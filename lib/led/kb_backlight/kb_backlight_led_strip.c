#include <lib/led/kb_backlight.h>

#include "kb_backlight.h"

#include <lib/keyboard/kb_mappings.h>
#include <lib/led/kb_bl_mode.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(kb_backlight_led_strip, CONFIG_KB_BACKLIGHT_LOG_LEVEL);

#define BRIGHTNESS_K                                                           \
    ((float)CONFIG_KB_BACKLIGHT_MAX_BRIGHTNESS_COMBINED / (255 * 3))

#define KB_BACKLIGHT_MIN_DELTA_MS (1000 / CONFIG_KB_BACKLIGHT_FPS)

const struct device *strip = DEVICE_DT_GET(DT_CHOSEN(ykb_backlight));

static int64_t last_update_time = 0;

static struct led_rgb frame[CONFIG_KB_KEY_COUNT];

static backlight_state state = {0};

static inline uint8_t brightness_scale(uint8_t v, uint8_t pct) {
    float scaled = (float)v * ((float)pct / 100.0f) * BRIGHTNESS_K;
    if (scaled < 0.f)
        scaled = 0.f;
    if (scaled > 255.f)
        scaled = 255.f;
    return (uint8_t)scaled;
}

static void apply_brightness(struct led_rgb *buf, size_t n, uint8_t pct) {
    for (size_t i = 0; i < n; ++i) {
        buf[i].r = brightness_scale(buf[i].r, pct);
        buf[i].g = brightness_scale(buf[i].g, pct);
        buf[i].b = brightness_scale(buf[i].b, pct);
    }
}

int kb_backlight_init() {
    if (!device_is_ready(strip)) {
        LOG_ERR("LED strip is not ready");
        return -1;
    }

    if (kb_bl_mode_count() == 0) {
        LOG_ERR("No backlight modes found");
        return -2;
    }

    state.mode_idx = 0;
    state.mode = kb_bl_mode_find("press_bruise_red");
    if (state.mode) {
        LOG_INF("Using backlight mode '%s'", state.mode->name);
        if (state.mode->init) {
            state.mode->init(CONFIG_KB_KEY_COUNT);
        }
    }

    state.on = true;
    state.brightness = 100;
    state.mode_speed = 1;

    LOG_INF("Successfully initialized backlight module");
    return 0;
}

int kb_backlight_set_mode(size_t mode_idx) {
    const size_t mode_count = kb_bl_mode_count();
    if (mode_count == 0)
        return -ENODEV;
    if (mode_idx >= mode_count)
        return -EINVAL;

    struct kb_bl_mode *next = kb_bl_mode_by_idx(mode_idx);
    if (!next)
        return -ENOENT;

    if (state.mode && state.mode->deinit) {
        LOG_DBG("Deinitializing current mode '%s'", state.mode->name);
        state.mode->deinit();
    }
    state.mode = next;
    state.mode_idx = mode_idx;
    LOG_INF("Selected backlight mode '%s'", state.mode->name);
    if (state.on && state.mode->init) {
        state.mode->init(CONFIG_KB_KEY_COUNT);
    }
    return 0;
}

void kb_backlight_next_mode(void) {
    if (!state.mode)
        return;
    size_t n = kb_bl_mode_count();
    if (!n)
        return;
    kb_backlight_set_mode((state.mode_idx + 1) % n);
}

void kb_backlight_prev_mode(void) {
    if (!state.mode)
        return;
    size_t n = kb_bl_mode_count();
    if (!n)
        return;
    kb_backlight_set_mode((state.mode_idx + n - 1) % n);
}

void kb_backlight_set_brightness(uint8_t brightness) {
    brightness = MIN(brightness, 100);
    brightness = MAX(brightness, 1);
    state.brightness = brightness;
}

void kb_backlight_toggle() {
    if (state.on) {
        kb_backlight_turn_off();
    } else {
        kb_backlight_turn_on();
    }
}

void kb_backlight_turn_on() {
    state.on = true;
    if (state.mode && state.mode->init) {
        state.mode->init(CONFIG_KB_KEY_COUNT);
    }
}
void kb_backlight_turn_off() {
    state.on = false;
    if (state.mode && state.mode->deinit) {
        state.mode->deinit();
    }
}

void kb_backlight_handle() {
    int64_t current = k_uptime_get();
    int64_t dt = current - last_update_time;
    if (dt < KB_BACKLIGHT_MIN_DELTA_MS) {
        return;
    }
    last_update_time = current;

    if (!state.on) {
        return;
    }

    if (!state.mode || !state.mode->apply) {
        return;
    }

    state.mode->apply(dt, state.mode_speed, frame);
    apply_brightness(frame, CONFIG_KB_KEY_COUNT, state.brightness);

    int res = led_strip_update_rgb(strip, frame, CONFIG_KB_KEY_COUNT);
    if (res) {
        LOG_ERR("Unable to update strip (err %d)", res);
    }
}

void kb_backlight_on_event(kb_key_t *key) {
    if (state.mode && state.mode->on_event) {
        state.mode->on_event(key);
    }
}
