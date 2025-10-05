#include <lib/led/kb_backlight.h>

#include <lib/led/modes/modes.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(kb_backlight_led_strip, CONFIG_KB_BACKLIGHT_LOG_LEVEL);

#define BRIGHTNESS_K (CONFIG_KB_BACKLIGHT_MAX_BRIGHTNESS_COMBINED / (255 * 3))

const struct device *strip = DEVICE_DT_GET(DT_CHOSEN(ykb_backlight));

typedef struct {

    struct kb_bl_mode *mode;

    uint8_t brightness;

} backlight_state;

static backlight_state state;

int kb_backlight_init() {
    if (!device_is_ready(strip)) {
        LOG_ERR("LED strip is not ready");
        return -1;
    }

    if (!state.mode) {
        state.mode = kb_bl_mode_by_idx(0);
    }

    state.brightness = 100;

    LOG_INF("Successfully initialized backlight module");
    return 0;
}

int kb_backlight_set_mode(size_t mode_idx) {
    struct kb_bl_mode *mode = kb_bl_mode_by_idx(mode_idx);
    if (!mode) {
        LOG_ERR("Unable to set backlight mode with index %d, no such mode",
                mode_idx);
    }
    return 0;
}
void kb_backlight_next_mode() {}
void kb_backlight_prev_mode() {}

void kb_backlight_set_brightness(uint8_t brightness) {}

void kb_backlight_toggle() {}
void kb_backlight_turn_on() {}
void kb_backlight_turn_off() {}
