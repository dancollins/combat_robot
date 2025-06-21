#ifndef MAIN_H
#define MAIN_H

enum led_state
{
    /* Just powered on. */
    LED_STATE_INIT,
    /* Radio is not connected. */
    LED_STATE_NO_RADIO,
    /* Radio is connected, but weapon is disabled. */
    LED_STATE_DISARMED,
    /* Radio is connected and weapon is hot! */
    LED_STATE_ARMED,
};

extern void led_set_state(enum led_state state);

#endif /* MAIN_H */
