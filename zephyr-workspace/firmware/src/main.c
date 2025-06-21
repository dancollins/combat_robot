#include "main.h"

#include <math.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>

#include "drivers/misc/csrf.h"

static const struct device *const wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));

static const struct device *elrs_radio =
    DEVICE_DT_GET(DT_NODELABEL(elrs_radio));

static const struct device *imu = DEVICE_DT_GET(DT_NODELABEL(accel_gyro));

static const struct pwm_dt_spec esc_pwm = PWM_DT_SPEC_GET(DT_NODELABEL(esc0));

static const struct pwm_dt_spec dc_motor_1 = PWM_DT_SPEC_GET(DT_NODELABEL(dc1));
static const struct pwm_dt_spec dc_motor_2 = PWM_DT_SPEC_GET(DT_NODELABEL(dc2));
static const struct pwm_dt_spec dc_motor_3 = PWM_DT_SPEC_GET(DT_NODELABEL(dc3));
static const struct pwm_dt_spec dc_motor_4 = PWM_DT_SPEC_GET(DT_NODELABEL(dc4));

static const struct gpio_dt_spec dc_motor_1_dir =
    GPIO_DT_SPEC_GET(DT_NODELABEL(dc_dir1), gpios);

static const struct gpio_dt_spec dc_motor_2_dir =
    GPIO_DT_SPEC_GET(DT_NODELABEL(dc_dir2), gpios);

static const struct gpio_dt_spec dc_motor_3_dir =
    GPIO_DT_SPEC_GET(DT_NODELABEL(dc_dir3), gpios);

static const struct gpio_dt_spec dc_motor_4_dir =
    GPIO_DT_SPEC_GET(DT_NODELABEL(dc_dir4), gpios);

static const struct gpio_dt_spec dc_motor_sleep =
    GPIO_DT_SPEC_GET(DT_NODELABEL(dc_sleep), gpios);

/* Work for killing the motors if the radio stops. */
static void stop_motors_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(stop_motors_work, stop_motors_work_handler);

static k_timeout_t stop_motors_timeout = K_MSEC(500);

static bool have_radio = false;
static bool armed = false;

struct rcmix_data
{
    uint32_t fleft, rleft;
    bool fleft_dir, rleft_dir;

    uint32_t fright, rright;
    bool fright_dir, rright_dir;

    uint32_t weapon_pulse;
} rcmix_data;

static void update_channel_data(struct rcmix_data *data,
                                const struct csrf_channel_data *channels)
{
    /* RC channels run 0-2047.
     * Weapon ESC is 1000-2000 us.
     * DC motors are 0-20000 us. */

    static unsigned steering_ch = 0;
    static unsigned throttle_ch = 2;
    static unsigned arm_btn = 7;
    static unsigned reverse_btn = 8;
    static unsigned weapon_ch = 9;

    float throttle = channels->ch[throttle_ch];
    float steering = channels->ch[steering_ch];
    bool reverse = channels->ch[reverse_btn] > 1024;
    uint16_t weapon = channels->ch[weapon_ch];

    armed = channels->ch[arm_btn] > 1024;

    /* Throttle deadband. */
    if (throttle < 200)
        throttle = 0;

    throttle /= 2048.0f;

    steering /= 2048.0f;
    steering -= 0.5f;

    float left = throttle + steering;
    float right = throttle - steering;

    left = CLAMP(left, -1.0f, 1.0f);
    right = CLAMP(right, -1.0f, 1.0f);

    data->fleft = (uint32_t)(fabsf(left) * 20000000);
    data->fleft = CLAMP(data->fleft, 0, 20000000);

    data->fright = (uint32_t)(fabsf(right) * 20000000);
    data->fright = CLAMP(data->fright, 0, 20000000);

    data->rleft = data->fleft;
    data->rright = data->fright;

    if (left > 0) {
        data->fleft_dir = false;
        data->rleft_dir = false;
    } else {
        data->fleft_dir = true;
        data->rleft_dir = true;
    }

    if (right > 0) {
        data->fright_dir = true;
        data->rright_dir = true;
    } else {
        data->fright_dir = false;
        data->rright_dir = false;
    }

    if (reverse) {
        data->fleft_dir = !data->fleft_dir;
        data->fright_dir = !data->fright_dir;
        data->rleft_dir = !data->rleft_dir;
        data->rright_dir = !data->rright_dir;
    }

    const unsigned weapon_pulse_min = 1000000;
    const unsigned weapon_pulse_max = 2000000;

    data->weapon_pulse = (weapon * 500) + 1000000;
    data->weapon_pulse =
        CLAMP(data->weapon_pulse, weapon_pulse_min, weapon_pulse_max);

    if (armed) {
        led_set_state(LED_STATE_ARMED);
    } else {
        led_set_state(LED_STATE_DISARMED);
        data->weapon_pulse = weapon_pulse_min;
    }
}

static void csrf_channel_callback(const struct csrf_channel_data *channels)
{
    update_channel_data(&rcmix_data, channels);

    have_radio = true;
    wdt_feed(wdt, 0);

    k_work_reschedule(&stop_motors_work, stop_motors_timeout);

    if (rcmix_data.fleft == 0 && rcmix_data.rleft == 0 &&
        rcmix_data.fright == 0 && rcmix_data.rright == 0) {
        gpio_pin_set_dt(&dc_motor_sleep, 1);
    } else {
        gpio_pin_set_dt(&dc_motor_sleep, 0);
    }

    /* Here's the mapping between logical channels and PWM channels! */
    gpio_pin_set_dt(&dc_motor_1_dir, rcmix_data.fleft_dir);
    gpio_pin_set_dt(&dc_motor_2_dir, !rcmix_data.fright_dir);
    gpio_pin_set_dt(&dc_motor_3_dir, rcmix_data.rright_dir);
    gpio_pin_set_dt(&dc_motor_4_dir, !rcmix_data.rleft_dir);

    pwm_set_pulse_dt(&dc_motor_1, rcmix_data.fleft);
    pwm_set_pulse_dt(&dc_motor_2, rcmix_data.fright);
    pwm_set_pulse_dt(&dc_motor_3, rcmix_data.rright);
    pwm_set_pulse_dt(&dc_motor_4, rcmix_data.rleft);

    pwm_set_pulse_dt(&esc_pwm, rcmix_data.weapon_pulse);
}

static void stop_motors_work_handler(struct k_work *work)
{
    pwm_set_pulse_dt(&dc_motor_1, 0);
    pwm_set_pulse_dt(&dc_motor_2, 0);
    pwm_set_pulse_dt(&dc_motor_3, 0);
    pwm_set_pulse_dt(&dc_motor_4, 0);

    pwm_set_pulse_dt(&esc_pwm, 1000000);

    led_set_state(LED_STATE_NO_RADIO);
}

static void init_watchdog(void)
{
    const unsigned wdt_min = 0;
    const unsigned wdt_max = 3000;

    int rc;

    if (!device_is_ready(wdt)) {
        printk("%s: device not ready.\n", wdt->name);
        return;
    }

    struct wdt_timeout_cfg wdt_config = {
        /* Reset SoC when watchdog timer expires. */
        .flags = WDT_FLAG_RESET_SOC,

        /* Expire watchdog after max window */
        .window.min = wdt_min,
        .window.max = wdt_max,
    };

    rc = wdt_install_timeout(wdt, &wdt_config);
    if (rc < 0) {
        printk("Failed to configure WDT.");
        return;
    }

    rc = wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
    if (rc < 0) {
        printk("Failed to setup WDT.");
        return;
    }
}

int main(void)
{
    init_watchdog();

    if (!device_is_ready(elrs_radio)) {
        printk("ELRS radio device not ready\n");
    }

    if (!device_is_ready(imu)) {
        printk("IMU not ready\n");
    }

    printk("Hello, world!\n");

    csrf_set_channel_callback(elrs_radio, csrf_channel_callback);

    /* Zero out our speeds until we've got control data. */
    memset(&rcmix_data, 0, sizeof(rcmix_data));
    rcmix_data.weapon_pulse = 1000000;

    gpio_pin_set_dt(&dc_motor_sleep, 1);

    gpio_pin_set_dt(&dc_motor_1_dir, 0);
    gpio_pin_set_dt(&dc_motor_2_dir, 0);
    gpio_pin_set_dt(&dc_motor_3_dir, 0);
    gpio_pin_set_dt(&dc_motor_4_dir, 0);

    pwm_set_pulse_dt(&dc_motor_1, 0);
    pwm_set_pulse_dt(&dc_motor_2, 0);
    pwm_set_pulse_dt(&dc_motor_3, 0);
    pwm_set_pulse_dt(&dc_motor_4, 0);

    pwm_set_pulse_dt(&esc_pwm, 1000000);

    led_set_state(LED_STATE_NO_RADIO);

    while (1) {
        wdt_feed(wdt, 0);
        k_sleep(K_SECONDS(1));
    }

    return 0;
}
