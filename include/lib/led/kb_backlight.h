#ifndef LIB_KB_BACKLIGHT_H_
#define LIB_KB_BACKLIGHT_H_

#include <autoconf.h>
#include <zephyr/drivers/led_strip.h>

enum kb_backlight_type {
    KB_BACKLIGHT_NONE = -1,
    KB_BACKLIGHT_LED = 0,
    KB_BACKLIGHT_LED_STRIP = 1,
};

int kb_backlight_set_mode(int mode);
void kb_backlight_next_mode();
void kb_backlight_prev_mode();

void kb_backlight_set_brightness(uint8_t brightness);

static inline void kb_backlight_set_brightness_min() {
    kb_backlight_set_brightness(0);
}
static inline void kb_backlight_set_brightness_low() {
    kb_backlight_set_brightness(25);
}
static inline void kb_backlight_set_brightness_mid() {
    kb_backlight_set_brightness(50);
}
static inline void kb_backlight_set_brightness_high() {
    kb_backlight_set_brightness(75);
}
static inline void kb_backlight_set_brightness_max() {
    kb_backlight_set_brightness(100);
}

void kb_backlight_toggle();
void kb_backlight_turn_on();
void kb_backlight_turn_off();

static inline enum kb_backlight_type kb_backlight_get_type() {
#if CONFIG_KB_BACKLIGHT_DEVICE_LED
    return KB_BACKLIGHT_LED;
#elif CONFIG_KB_BACKLIGHT_DEVICE_LED_STRIP
    return KB_BACKLIGHT_LED_STRIP;
#else
    return KB_BACKLIGHT_NONE;
#endif // CONFIG_KB_BACKLIGHT_DEVICE
}

int kb_backlight_init();

#endif // LIB_KB_BACKLIGHT_H_
