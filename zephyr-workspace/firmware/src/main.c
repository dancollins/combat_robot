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

struct rcmix_data
{
    uint32_t dc_motor_pulse[4];
    bool dc_motor_dir[4];

    uint32_t weapon_pulse;
} rcmix_data;

static int update_channel_data(
    struct rcmix_data *data,
    uint16_t throttle_channel,
    uint16_t steering_channel,
    uint16_t weapon_channel)
{
    /* RC channels run 0-2047.
     * Weapon ESC is 1000-2000 us.
     * DC motors are 0-20000 us. */

    /* Throttle deadband. */
    if (throttle_channel < 200)
        throttle_channel = 0;

    float throttle = throttle_channel;
    throttle /= 2048.0f;

    float steering = steering_channel;
    steering /= 2048.0f;
    steering -= 0.5f;

    float left = throttle + steering;
    float right = throttle - steering;

    left = CLAMP(left, -1.0f, 1.0f);
    right = CLAMP(right, -1.0f, 1.0f);

    data->dc_motor_pulse[0] = (uint32_t)(fabs(left) * 20000000);
    data->dc_motor_pulse[0] = CLAMP(data->dc_motor_pulse[0], 0, 20000000);

    data->dc_motor_pulse[2] = (uint32_t)(fabs(right) * 20000000);
    data->dc_motor_pulse[2] = CLAMP(data->dc_motor_pulse[2], 0, 20000000);

    data->dc_motor_pulse[1] = data->dc_motor_pulse[0];
    data->dc_motor_pulse[3] = data->dc_motor_pulse[2];

    if (left > 0)
    {
        data->dc_motor_dir[0] = true;
        data->dc_motor_dir[1] = true;
    }
    else
    {
        data->dc_motor_dir[0] = false;
        data->dc_motor_dir[1] = false;
    }

    if (right > 0)
    {
        data->dc_motor_dir[2] = true;
        data->dc_motor_dir[3] = true;
    }
    else
    {
        data->dc_motor_dir[2] = false;
        data->dc_motor_dir[3] = false;
    }

    data->weapon_pulse = (weapon_channel * 500) + 1000000;
    if (data->weapon_pulse < 1100000)
        data->weapon_pulse = 1000000;
    if (data->weapon_pulse > 2000000)
        data->weapon_pulse = 2000000;
}

static void csrf_channel_callback(const struct csrf_channel_data *channels)
{
    update_channel_data(
        &rcmix_data,
        channels->ch[2],
        channels->ch[0],
        channels->ch[9]);

    if (rcmix_data.dc_motor_pulse[0] == 0 &&
        rcmix_data.dc_motor_pulse[1] == 0 &&
        rcmix_data.dc_motor_pulse[2] == 0 &&
        rcmix_data.dc_motor_pulse[3] == 0)
    {
        gpio_pin_set_dt(&dc_motor_sleep, 1);
    }
    else
    {
        gpio_pin_set_dt(&dc_motor_sleep, 0);
    }

    gpio_pin_set_dt(&dc_motor_1_dir, rcmix_data.dc_motor_dir[0]);
    gpio_pin_set_dt(&dc_motor_2_dir, rcmix_data.dc_motor_dir[1]);
    gpio_pin_set_dt(&dc_motor_3_dir, rcmix_data.dc_motor_dir[2]);
    gpio_pin_set_dt(&dc_motor_4_dir, rcmix_data.dc_motor_dir[3]);

    pwm_set_pulse_dt(&dc_motor_1, rcmix_data.dc_motor_pulse[0]);
    pwm_set_pulse_dt(&dc_motor_2, rcmix_data.dc_motor_pulse[1]);
    pwm_set_pulse_dt(&dc_motor_3, rcmix_data.dc_motor_pulse[2]);
    pwm_set_pulse_dt(&dc_motor_4, rcmix_data.dc_motor_pulse[3]);

    pwm_set_pulse_dt(&esc_pwm, rcmix_data.weapon_pulse);

    static int print = 0;
    if (print++ % 1500 == 0)
    {
        printk("dc %u,%u,%u,%u\n",
               rcmix_data.dc_motor_pulse[0],
               rcmix_data.dc_motor_pulse[1],
               rcmix_data.dc_motor_pulse[2],
               rcmix_data.dc_motor_pulse[3]);

        printk("wep %u\n", rcmix_data.weapon_pulse);

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

    while (1)
    {
        k_sleep(K_SECONDS(1));
    }

    return 0;
}
