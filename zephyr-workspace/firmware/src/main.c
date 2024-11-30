#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/uart.h>

#include "drivers/misc/csrf.h"

static const struct device *esc_uart = DEVICE_DT_GET(DT_CHOSEN(combat_esc_uart));

static const struct device *elrs_radio = DEVICE_DT_GET(DT_NODELABEL(elrs_radio));
static const struct device *spi_flash = DEVICE_DT_GET(DT_NODELABEL(spi_flash));
static const struct device *imu = DEVICE_DT_GET(DT_NODELABEL(accel_gyro));
static const struct device *rgb = DEVICE_DT_GET(DT_PATH(rgb_led));

static const struct pwm_dt_spec esc_pwm = PWM_DT_SPEC_GET(DT_NODELABEL(esc0));

static const struct pwm_dt_spec dc_motor_1 = PWM_DT_SPEC_GET(DT_NODELABEL(dc1));
static const struct pwm_dt_spec dc_motor_2 = PWM_DT_SPEC_GET(DT_NODELABEL(dc2));
static const struct pwm_dt_spec dc_motor_3 = PWM_DT_SPEC_GET(DT_NODELABEL(dc3));
static const struct pwm_dt_spec dc_motor_4 = PWM_DT_SPEC_GET(DT_NODELABEL(dc4));

static const struct gpio_dt_spec dc_motor_1_dir = GPIO_DT_SPEC_GET(DT_NODELABEL(dc_dir1), gpios);
static const struct gpio_dt_spec dc_motor_2_dir = GPIO_DT_SPEC_GET(DT_NODELABEL(dc_dir2), gpios);
static const struct gpio_dt_spec dc_motor_3_dir = GPIO_DT_SPEC_GET(DT_NODELABEL(dc_dir3), gpios);
static const struct gpio_dt_spec dc_motor_4_dir = GPIO_DT_SPEC_GET(DT_NODELABEL(dc_dir4), gpios);

static const struct gpio_dt_spec dc_motor_sleep = GPIO_DT_SPEC_GET(DT_NODELABEL(dc_sleep), gpios);

static int test_rgb(const struct device *rgb_dev)
{
    const uint8_t colours[9][3] = {
        {100, 0, 0},
        {100, 50, 0},
        {100, 100, 0},
        {0, 100, 0},
        {0, 0, 100},
        {50, 0, 100},
        {100, 0, 80},
        {100, 100, 100},
        {0, 0, 0}};

    k_timeout_t delay = K_MSEC(500);

    printk("Testing RGB device %s\n", rgb_dev->name);

    if (!device_is_ready(rgb_dev))
    {
        printk("RGB device not ready\n");
        return -1;
    }

    for (unsigned i = 0; i < ARRAY_SIZE(colours); i++)
    {
        led_set_brightness(rgb_dev, 0, colours[i][0]);
        led_set_brightness(rgb_dev, 1, colours[i][1]);
        led_set_brightness(rgb_dev, 2, colours[i][2]);
        k_sleep(delay);
    }

    return 0;
}

static int test_flash(const struct device *flash_dev)
{
    const uint8_t expected[] = {0xdc, 0x00, 0x11, 0x22, 0x33, 0x44};
    int rc;
    uint8_t buf[sizeof(expected)];

    printk("Testing flash device %s\n", flash_dev->name);

    if (!device_is_ready(flash_dev))
    {
        printk("Flash device not ready\n");
        return -1;
    }

    printk("Erasing flash...\n");
    rc = flash_erase(flash_dev, 0, 4096);
    if (rc != 0)
    {
        printk("Flash erase failed! %d\n", rc);
        return rc;
    }

    printk("Writing flash...\n");
    rc = flash_write(flash_dev, 0, expected, sizeof(expected));
    if (rc != 0)
    {
        printk("Flash write failed! %d\n", rc);
        return rc;
    }

    printk("Reading flash...\n");
    rc = flash_read(flash_dev, 0, buf, sizeof(expected));
    if (rc != 0)
    {
        printk("Flash read failed! %d\n", rc);
        return rc;
    }

    printk("Comparing flash...\n");
    for (size_t i = 0; i < sizeof(expected); i++)
    {
        bool match = expected[i] == buf[i];

        printk(
            "[%d] wrote %02x, read %02x (%s)\n",
            i,
            expected[i],
            buf[i],
            match ? "match" : "mismatch");
    }

    return 0;
}

static int test_esc(void)
{
    uint32_t pulse_ns_a = 1000 * NSEC_PER_USEC;
    uint32_t pulse_ns_b = 1200 * NSEC_PER_USEC;

    printk("Testing ESC PWM device %s\n", esc_pwm.dev->name);

    if (!pwm_is_ready_dt(&esc_pwm))
    {
        printk("Error: PWM device %s is not ready\n", esc_pwm.dev->name);
        return -ENODEV;
    }

    // printk("Setting ESC pulse width to %d ns\n", pulse_ns_a);
    // pwm_set_pulse_dt(&esc_pwm, pulse_ns_a);
    // k_sleep(K_SECONDS(3));

    // printk("Setting ESC pulse width to %d ns\n", pulse_ns_b);
    // pwm_set_pulse_dt(&esc_pwm, pulse_ns_b);
    // k_sleep(K_SECONDS(3));

    printk("Setting ESC pulse width to %d ns\n", pulse_ns_a);
    pwm_set_pulse_dt(&esc_pwm, pulse_ns_a);

    return 0;
}

static int test_dc_motor(void)
{
    uint32_t pulse_ns_a = 0;
    uint32_t pulse_ns_b = 10000 * NSEC_PER_USEC;

    printk("Testing DC motor PWM device %s\n", dc_motor_3.dev->name);

    if (!pwm_is_ready_dt(&dc_motor_1))
    {
        printk("Error: PWM device %s is not ready\n", dc_motor_1.dev->name);
        return -ENODEV;
    }

    if (!pwm_is_ready_dt(&dc_motor_2))
    {
        printk("Error: PWM device %s is not ready\n", dc_motor_2.dev->name);
        return -ENODEV;
    }

    if (!pwm_is_ready_dt(&dc_motor_3))
    {
        printk("Error: PWM device %s is not ready\n", dc_motor_3.dev->name);
        return -ENODEV;
    }

    if (!pwm_is_ready_dt(&dc_motor_4))
    {
        printk("Error: PWM device %s is not ready\n", dc_motor_4.dev->name);
        return -ENODEV;
    }

    if (!gpio_is_ready_dt(&dc_motor_1_dir))
    {
        printk("Error: GPIO device %s is not ready\n", dc_motor_1_dir.port->name);
        return -ENODEV;
    }

    if (!gpio_is_ready_dt(&dc_motor_2_dir))
    {
        printk("Error: GPIO device %s is not ready\n", dc_motor_2_dir.port->name);
        return -ENODEV;
    }

    if (!gpio_is_ready_dt(&dc_motor_3_dir))
    {
        printk("Error: GPIO device %s is not ready\n", dc_motor_3_dir.port->name);
        return -ENODEV;
    }

    if (!gpio_is_ready_dt(&dc_motor_4_dir))
    {
        printk("Error: GPIO device %s is not ready\n", dc_motor_4_dir.port->name);
        return -ENODEV;
    }

    if (!gpio_is_ready_dt(&dc_motor_sleep))
    {
        printk("Error: GPIO device %s is not ready\n", dc_motor_sleep.port->name);
        return -ENODEV;
    }

    gpio_pin_configure_dt(&dc_motor_sleep, GPIO_OUTPUT);
    gpio_pin_set_dt(&dc_motor_sleep, 1);

    gpio_pin_configure_dt(&dc_motor_1_dir, GPIO_OUTPUT);
    gpio_pin_configure_dt(&dc_motor_2_dir, GPIO_OUTPUT);
    gpio_pin_configure_dt(&dc_motor_3_dir, GPIO_OUTPUT);
    gpio_pin_configure_dt(&dc_motor_4_dir, GPIO_OUTPUT);

    gpio_pin_set_dt(&dc_motor_1_dir, 0);
    gpio_pin_set_dt(&dc_motor_2_dir, 0);
    gpio_pin_set_dt(&dc_motor_3_dir, 0);
    gpio_pin_set_dt(&dc_motor_4_dir, 0);

    // printk("Setting DC pulse width to %d ns\n", pulse_ns_a);
    // pwm_set_pulse_dt(&dc_motor_1, pulse_ns_a);
    // pwm_set_pulse_dt(&dc_motor_2, pulse_ns_a);
    // pwm_set_pulse_dt(&dc_motor_3, pulse_ns_a);
    // pwm_set_pulse_dt(&dc_motor_4, pulse_ns_a);
    // k_sleep(K_SECONDS(1));

    // printk("Setting DC pulse width to %d ns\n", pulse_ns_b);
    // pwm_set_pulse_dt(&dc_motor_1, pulse_ns_b);
    // pwm_set_pulse_dt(&dc_motor_2, pulse_ns_b);
    // pwm_set_pulse_dt(&dc_motor_3, pulse_ns_b);
    // pwm_set_pulse_dt(&dc_motor_4, pulse_ns_b);
    // k_sleep(K_SECONDS(2));

    // printk("Setting DC pulse width to %d ns\n", pulse_ns_a);
    // pwm_set_pulse_dt(&dc_motor_1, pulse_ns_a);
    // pwm_set_pulse_dt(&dc_motor_2, pulse_ns_a);
    // pwm_set_pulse_dt(&dc_motor_3, pulse_ns_a);
    // pwm_set_pulse_dt(&dc_motor_4, pulse_ns_a);
    // k_sleep(K_SECONDS(1));

    // gpio_pin_set_dt(&dc_motor_1_dir, 1);
    // gpio_pin_set_dt(&dc_motor_2_dir, 1);
    // gpio_pin_set_dt(&dc_motor_3_dir, 1);
    // gpio_pin_set_dt(&dc_motor_4_dir, 1);

    // printk("Setting DC pulse width to %d ns\n", pulse_ns_b);
    // pwm_set_pulse_dt(&dc_motor_1, pulse_ns_b);
    // pwm_set_pulse_dt(&dc_motor_2, pulse_ns_b);
    // pwm_set_pulse_dt(&dc_motor_3, pulse_ns_b);
    // pwm_set_pulse_dt(&dc_motor_4, pulse_ns_b);
    // k_sleep(K_SECONDS(2));

    // printk("Setting DC pulse width to %d ns\n", pulse_ns_a);
    // pwm_set_pulse_dt(&dc_motor_1, pulse_ns_a);
    // pwm_set_pulse_dt(&dc_motor_2, pulse_ns_a);
    // pwm_set_pulse_dt(&dc_motor_3, pulse_ns_a);
    // pwm_set_pulse_dt(&dc_motor_4, pulse_ns_a);

    return 0;
}

static void csrf_channel_callback(const struct csrf_channel_data *channels)
{
    bool red_on = channels->ch[7] > 1000;
    if (red_on)
        led_set_brightness(rgb, 0, 100);
    else
        led_set_brightness(rgb, 0, 0);

    /* Channel range is 0-2048, motor speed is 0-20000 us */
    uint32_t dc0_speed = channels->ch[2] * 10;
    if (dc0_speed > 20000)
        dc0_speed = 20000;
    else if (dc0_speed < 2000)
        dc0_speed = 0;

    if (dc0_speed > 0)
    {
        gpio_pin_set_dt(&dc_motor_sleep, 0);
        gpio_pin_set_dt(&dc_motor_1_dir, 0);
        pwm_set_pulse_dt(&dc_motor_1, dc0_speed * NSEC_PER_USEC);
    }
    else
    {
        pwm_set_pulse_dt(&dc_motor_1, 0);
        gpio_pin_set_dt(&dc_motor_sleep, 1);
    }

    /* Channel range is 0-2048, weapon is 1000-2000 us */
    uint32_t weapon_speed = (channels->ch[9] / 2) + 1000;

    if (weapon_speed > 2000)
        weapon_speed = 2000;
    else if (weapon_speed < 1200)
        weapon_speed = 1000;

    pwm_set_pulse_dt(&esc_pwm, weapon_speed * NSEC_PER_USEC);

    static int print = 0;
    if (print++ % 1000 == 0)
    {
        printk("%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
               channels->ch[0],
               channels->ch[1],
               channels->ch[2],
               channels->ch[3],
               channels->ch[4],
               channels->ch[5],
               channels->ch[6],
               channels->ch[7],
               channels->ch[8],
               channels->ch[9],
               channels->ch[10],
               channels->ch[11],
               channels->ch[12],
               channels->ch[13],
               channels->ch[14],
               channels->ch[15]);
    }
}

int main(void)
{
    if (!device_is_ready(elrs_radio))
    {
        printk("ELRS radio device not ready\n");
    }

    if (!device_is_ready(imu))
    {
        printk("IMU not ready\n");
    }

    printk("Hello, world!\n");

    csrf_set_channel_callback(elrs_radio, csrf_channel_callback);

    // test_rgb(rgb);
    // test_flash(spi_flash);
    test_esc();
    test_dc_motor();

    while (1)
    {
        k_sleep(K_SECONDS(1));
    }

    return 0;
}
