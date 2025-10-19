#include <lib/led/kb_backlight_settings.h>

#include <lib/led/kb_backlight_state.h>

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

extern backlight_state bl_state;

static bool s_loaded_ok = false;

static void kb_backlight_settings_load_default() {
    bl_state.brightness = 100;
    bl_state.mode_idx = 0;
    bl_state.mode_speed = 1;
    bl_state.on = true;
}

void kb_backlight_settings_build_image_from_runtime(backlight_state_img *img) {
    img->version = KB_BL_SETTINGS_IMAGE_VERSION;
    img->on = bl_state.on;
    img->brightness = bl_state.brightness;
    img->mode_speed = bl_state.mode_speed;
    img->mode_idx = bl_state.mode_idx;
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

    bl_state.brightness = img.brightness;
    bl_state.mode_idx = img.mode_idx;
    bl_state.mode_speed = img.mode_speed;
    bl_state.on = img.on;

    s_loaded_ok = true;

    return 0;
}

static int kb_bl_settings_export(int (*export_func)(const char *name,
                                                    const void *val,
                                                    size_t val_len)) {
    backlight_state_img img;
    kb_backlight_settings_build_image_from_runtime(&img);
    return export_func(KB_BL_SETTINGS_ITEM, &img, sizeof(img));
}

static struct settings_handler kb_bl_settings_handler = {
    .name = KB_BL_SETTINGS_NS,
    .h_set = kb_bl_settings_set,
    .h_export = kb_bl_settings_export,
};

void kb_bl_settings_save(void) {
    backlight_state_img img;
    kb_backlight_settings_build_image_from_runtime(&img);

    int w = settings_save_one(KB_BL_SETTINGS_KEY, &img, sizeof(img));
    if (w) {
        LOG_WRN("Could not save default backlight state: %d", w);
    }
}

void kb_backlight_settings_init(void) {
    int err;

    s_loaded_ok = false;

    err = settings_subsys_init();
    if (err) {
        LOG_ERR("settings_subsys_init failed: %d", err);
        goto load_defaults;
    }

    err = settings_register(&kb_bl_settings_handler);
    if (err) {
        LOG_ERR("settings_register failed: %d", err);
        goto load_defaults;
    }

    err = settings_load_subtree(KB_BL_SETTINGS_NS);

    if (!s_loaded_ok) {
        LOG_WRN("No valid backlight settings found (err=%d) — loading defaults",
                err);
        goto load_defaults;
    }

    return;

load_defaults:

    kb_backlight_settings_load_default();

    backlight_state_img img;
    kb_backlight_settings_build_image_from_runtime(&img);

    int w = settings_save_one(KB_BL_SETTINGS_KEY, &img, sizeof(img));
    if (w) {
        LOG_WRN("Could not save default backlight state: %d", w);
    } else {
        LOG_INF("Default backlight settings saved.");
    }
}
