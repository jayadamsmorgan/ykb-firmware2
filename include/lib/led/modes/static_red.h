#include <lib/led/modes/modes.h>

typedef struct {
    struct kb_bl_rgb rgb;
    size_t len;
} static_red_data;

static static_red_data data = {0};

static void init(size_t len, uint8_t brightness) {
    data.len = len;
    brightness = MAX(brightness, 100);
    data.rgb.r = 255 / 100 * brightness;
}

static void deinit() {
    data.rgb.r = 0;
}

static void apply(uint32_t dt_ms, struct kb_bl_rgb *frame, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        frame[i] = data.rgb;
    }
}

KB_BL_MODE_DEFINE(static_red, init, deinit, apply, NULL);
