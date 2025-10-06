#include <lib/led/kb_bl_mode.h>

typedef struct {
    struct led_rgb rgb;
    size_t len;
    int8_t breathe_bright;
    bool inc;
} static_red_data;

static static_red_data data = {0};

static void init(size_t len) {
    data.len = len;
    data.rgb = (struct led_rgb){
        .r = 255,
        .g = 0,
        .b = 0,
    };
    data.breathe_bright = 0;
    data.inc = true;
}

static void deinit() {
    data.rgb.r = 0;
}

static void apply(uint32_t dt_ms, float speed, struct led_rgb *frame) {
    for (size_t i = 0; i < data.len; ++i) {
        frame[i] = data.rgb;
        frame[i].r *= (double)data.breathe_bright / 100;
    }

    if (data.inc) {
        data.breathe_bright += 1;
        if (data.breathe_bright >= 100) {
            data.breathe_bright = 100;
            data.inc = false;
        }

        return;
    }

    data.breathe_bright -= 1;
    if (data.breathe_bright <= 0) {
        data.breathe_bright = 0;
        data.inc = true;
    }
}

// KB_BL_MODE_DEFINE(breathe_red, init, deinit, apply, NULL);
