#include <lib/led/kb_backlight.h>

#include <lib/led/modes/modes.h>

#include YKB_LEDS_GEOM_PATH

int kb_backlight_init() {
    return 0;
}

int kb_backlight_set_mode(int mode) {
    return 0;
}
void kb_backlight_next_mode() {}
void kb_backlight_prev_mode() {}

void kb_backlight_set_brightness(uint8_t brightness) {}

void kb_backlight_toggle() {}
void kb_backlight_turn_on() {}
void kb_backlight_turn_off() {}
