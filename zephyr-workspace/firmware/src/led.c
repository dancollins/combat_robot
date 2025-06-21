#include <zephyr/drivers/led.h>
#include <zephyr/kernel.h>

#include "main.h"

static const struct device *rgb = DEVICE_DT_GET(DT_PATH(rgb_led));

/**
 * @brief A colour in RGB space.
 */
struct rgb_colour
{
    uint8_t r, g, b;
};

/**
 * @brief A frame in the LED animation.
 */
struct frame
{
    /* The colour to set the LED to. */
    struct rgb_colour colour;
    /* How long to stay in this frame. */
    unsigned duration_ms;
};

#define RGB(r_, g_, b_) {.r = r_, .g = g_, .b = b_}

#define LED_PATTERN_BLINK2(colour_, interval_on_, interval_off_) \
    {                                                            \
        {.colour = colour_, .duration_ms = interval_on_},        \
        {                                                        \
            .colour = RGB(0, 0, 0), .duration_ms = interval_off_ \
        }                                                        \
    }

#define LED_PATTERN_BLINK(colour_, interval_)                \
    {                                                        \
        {.colour = colour_, .duration_ms = interval_},       \
        {                                                    \
            .colour = RGB(0, 0, 0), .duration_ms = interval_ \
        }                                                    \
    }

static const struct frame pattern_init[] = {
    {.colour = RGB(100, 100, 100), .duration_ms = 500}};

static const struct frame pattern_no_radio[] =
    LED_PATTERN_BLINK2(RGB(0, 0, 100), 30, 1000);

static const struct frame pattern_disarmed[] =
    LED_PATTERN_BLINK2(RGB(0, 100, 0), 30, 500);

static const struct frame pattern_armed[] =
    LED_PATTERN_BLINK2(RGB(100, 0, 0), 30, 100);

static enum led_state current_state = LED_STATE_INIT;
static const struct frame *current_pattern = NULL;
static unsigned frame_index = 0;
static unsigned pattern_length = 0;
static unsigned time_ms = 0;

static void led_timer_handler(struct k_timer *timer);
K_TIMER_DEFINE(led_timer, led_timer_handler, NULL);

static void led_work_handler(struct k_work *work);
K_WORK_DEFINE(led_work, led_work_handler);

static void led_timer_handler(struct k_timer *timer)
{
    k_work_submit(&led_work);
}

static void led_work_handler(struct k_work *work)
{
    const struct frame *frame = &current_pattern[frame_index];

    led_set_brightness(rgb, 0, frame->colour.r);
    led_set_brightness(rgb, 1, frame->colour.g);
    led_set_brightness(rgb, 2, frame->colour.b);

    time_ms += 10;
    if (time_ms >= frame->duration_ms) {
        time_ms = 0;
        frame_index++;
    }

    if (frame_index >= pattern_length) {
        frame_index = 0;
    }
}

void led_set_state(enum led_state state)
{
    if (current_state == state) {
        return;
    }

    switch (state) {
        case LED_STATE_NO_RADIO:
            current_pattern = pattern_no_radio;
            pattern_length = ARRAY_SIZE(pattern_no_radio);
            break;

        case LED_STATE_DISARMED:
            current_pattern = pattern_disarmed;
            pattern_length = ARRAY_SIZE(pattern_disarmed);
            break;

        case LED_STATE_ARMED:
            current_pattern = pattern_armed;
            pattern_length = ARRAY_SIZE(pattern_armed);
            break;

        case LED_STATE_INIT:
        default:
            current_pattern = pattern_init;
            pattern_length = ARRAY_SIZE(pattern_init);
            break;
    }

    frame_index = 0;
    current_state = state;
}

static int led_init(void)
{
    if (!device_is_ready(rgb)) {
        return -ENODEV;
    }

    current_pattern = pattern_init;
    pattern_length = ARRAY_SIZE(pattern_init);
    frame_index = 0;
    current_state = LED_STATE_INIT;

    k_timer_start(&led_timer, K_MSEC(10), K_MSEC(10));

    return 0;
}

SYS_INIT(led_init, APPLICATION, 99);
