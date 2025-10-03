#ifndef LIB_KB_BACKLIGHT_MODES_H_
#define LIB_KB_BACKLIGHT_MODES_H_

#include <zephyr/kernel.h>
#include <zephyr/sys/iterable_sections.h>

#include <lib/led/color.h>

#include <lib/keyboard/kb_key.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct kb_bl_mode {
    void (*init)(size_t len, uint8_t brightness);
    void (*deinit)();
    void (*tick)(uint32_t dt_ms);
    void (*apply)(struct kb_bl_rgb *frame, size_t len);
    void (*on_event)(kb_key_t *key, bool pressed);
    void (*set_brightness)(uint8_t brightness);
    const char *name;
};

static inline struct kb_bl_mode *kb_bl_mode_find(const char *name) {
    STRUCT_SECTION_FOREACH(kb_bl_mode, mode) {
        if (strcmp(name, mode->name) == 0) {
            return mode;
        }
    }
    return NULL;
}

static inline struct kb_bl_mode *kb_bl_mode_by_idx(size_t idx) {

    size_t mode_count;
    STRUCT_SECTION_COUNT(kb_bl_mode, &mode_count);
    if (idx >= mode_count)
        return NULL;

    struct kb_bl_mode *ret;
    STRUCT_SECTION_GET(kb_bl_mode, idx, &ret);

    return ret;
}

#define KB_BL_MODE_DEFINE(mode_name, init_fn, deinit_fn, tick_fn, apply_fn,    \
                          on_event_fn, set_brightness_fn)                      \
    static STRUCT_SECTION_ITERABLE(kb_bl_mode, __kb_bl_mode_##mode_name) = {   \
        .name = STRINGIFY(mode_name),                                          \
        .init = init_fn,                                                       \
        .deinit = deinit_fn,                                                   \
        .tick = tick_fn,                                                       \
        .apply = apply_fn,                                                     \
        .on_event = on_event_fn,                                               \
        .set_brightness = set_brightness_fn,                                   \
    }

#endif // LIB_KB_BACKLIGHT_MODES_H_
