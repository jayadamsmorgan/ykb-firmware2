#include "kb_backlight.h"

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

LOG_MODULE_DECLARE(kb_backlight_led_strip, CONFIG_KB_BACKLIGHT_LOG_LEVEL);

/*
 * Namespace: "bl"
 * Item key : "blob"
 * Full path: "bl/blob"
 */
#define KB_BL_SETTINGS_NS "bl"
#define KB_BL_SETTINGS_ITEM "blob"
#define KB_BL_SETTINGS_KEY KB_BL_SETTINGS_NS "/" KB_BL_SETTINGS_ITEM

#define KB_BL_SETTINGS_IMAGE_VERSION 1

typedef struct {
    uint16_t version;

    uint16_t mode_idx;
    float mode_speed;
    uint8_t brightness;
    bool on;

} backlight_state_img;

extern backlight_state state;

static void kb_backlight_settings_load_default(backlight_state *state) {
    state->brightness = 100;
    state->mode_idx = 0;
    state->mode_speed = 0.5f;
    state->on = true;
}

static void build_image_from_runtime(backlight_state_img *img) {
    img->version = KB_BL_SETTINGS_IMAGE_VERSION;
    img->on = state.on;
    img->brightness = state.brightness;
    img->mode_speed = state.mode_speed;
    img->mode_idx = state.mode_idx;
}

static int kb_bl_settings_set(const char *key, size_t len,
                              settings_read_cb read_cb, void *cb_arg) {
    if (strcmp(key, KB_BL_SETTINGS_ITEM) != 0) {
        return -ENOENT;
    }

    const size_t backlight_state_img_size = sizeof(backlight_state_img);
    if (len != backlight_state_img_size) {
        LOG_ERR("backlight settings image size mismatch: got %zu, want %zu",
                len, backlight_state_img_size);
        return -EINVAL;
    }

    backlight_state_img img;
    ssize_t rlen = read_cb(cb_arg, &img, sizeof(img));

    if (rlen < 0) {
        LOG_ERR("backlight state read_cb error: %d", (int)rlen);
        return (int)rlen;
    }
    if ((size_t)rlen != sizeof(img)) {
        LOG_ERR("backlight state truncated: %zd", rlen);
        return -EINVAL;
    }

    if (img.version != KB_BL_SETTINGS_IMAGE_VERSION) {
        LOG_ERR("backlight state versiom mismatch: got %u, want %u",
                img.version, KB_BL_SETTINGS_IMAGE_VERSION);
        return -EINVAL;
    }

    return 0;
}

static int kb_bl_settings_export(int (*export_func)(const char *name,
                                                    const void *val,
                                                    size_t val_len)) {
    backlight_state_img img;
    build_image_from_runtime(&img);
    return export_func(KB_BL_SETTINGS_ITEM, &img, sizeof(img));
}

static struct settings_handler kb_bl_settings_handler = {
    .name = KB_BL_SETTINGS_NS,
    .h_set = kb_bl_settings_set,
    .h_export = kb_bl_settings_export,
};

static bool s_loaded_ok = false;

int kb_backlight_settings_init(backlight_state *state) {
    int err;

    err = settings_subsys_init();
    if (err) {
        LOG_ERR("settings_subsys_init failed: %d", err);
        // continue; we will still try to load/save
    }

    err = settings_register(&kb_bl_settings_handler);
    if (err) {
        LOG_ERR("settings_register failed: %d", err);
        // continue; we can still run with defaults
    }

    s_loaded_ok = false;
    err = settings_load_subtree(KB_BL_SETTINGS_NS);

    if (!s_loaded_ok) {
        LOG_WRN("No valid backlight settings found (err=%d) — loading defaults",
                err);

        kb_backlight_settings_load_default(state);

        backlight_state_img img;
        build_image_from_runtime(state, &img);

        int w = settings_save_one(KB_BL_SETTINGS_KEY, &img, sizeof(img));
        if (w) {
            LOG_WRN("Could not save default backlight state: %d", w);
        } else {
            LOG_INF("Default backlight settings saved.");
        }
    }

    return 0;
}
