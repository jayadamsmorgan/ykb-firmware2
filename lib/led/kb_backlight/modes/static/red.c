#include <lib/led/kb_bl_mode.h>

typedef struct {
    struct led_rgb rgb;
    size_t len;
} static_red_data;

static static_red_data data = {0};

static void init(size_t len) {
    data.len = len;
    data.rgb = (struct led_rgb){
        .r = 255,
        .g = 0,
        .b = 0,
    };
}

static void deinit() {
    data.rgb.r = 0;
}

static void apply(uint32_t dt_ms, float speed, struct led_rgb *frame) {
    for (size_t i = 0; i < data.len; ++i) {
        frame[i] = data.rgb;
    }
}

KB_BL_MODE_DEFINE(static_red, init, deinit, apply, NULL);
