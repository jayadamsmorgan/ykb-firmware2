#include <lib/keyboard/kb_settings.h>

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/toolchain.h>

#include <stdint.h>
#include <string.h>

LOG_MODULE_REGISTER(kb_settings, CONFIG_KB_SETTINGS_LOG_LEVEL);

#include YKB_DEF_MAPPINGS_PATH

BUILD_ASSERT(CONFIG_KB_SETTINGS_DEFAULT_MINIMUM <
                 CONFIG_KB_SETTINGS_DEFAULT_MAXIMUM,
             "Default value for key not pressed should be less than default "
             "value for key pressed fully.");

static kb_settings_t settings;

/* -------- Settings subsystem keys --------
 * Namespace: "kb"
 * Item key : "blob"
 * Full path: "kb/blob"
 */
#define KB_SETTINGS_NS "kb"
#define KB_SETTINGS_ITEM "blob"
#define KB_SETTINGS_KEY KB_SETTINGS_NS "/" KB_SETTINGS_ITEM

/* Versioned image so we can change kb_settings_t later */
#define KB_SETTINGS_IMAGE_VERSION 1
struct kb_settings_image {
    uint16_t version;
    kb_settings_t settings;
};

/* ------------ Defaults (unchanged) ------------- */
static void kb_settings_load_default(void) {
    memset(&settings, 0, sizeof(settings));

    settings.key_polling_rate = CONFIG_KB_SETTINGS_DEFAULT_POLLING_RATE;

    float range = (float)(CONFIG_KB_SETTINGS_DEFAULT_MAXIMUM -
                          CONFIG_KB_SETTINGS_DEFAULT_MINIMUM);
    float default_threshold =
        ((range / 100.0f) * CONFIG_KB_SETTINGS_DEFAULT_THRESHOLD) +
        (float)CONFIG_KB_SETTINGS_DEFAULT_MINIMUM;

    for (size_t i = 0; i < CONFIG_KB_KEY_COUNT; ++i) {
        settings.key_thresholds[i] = (uint16_t)default_threshold;
        settings.minimums[i] = CONFIG_KB_SETTINGS_DEFAULT_MINIMUM;
        settings.maximums[i] = CONFIG_KB_SETTINGS_DEFAULT_MAXIMUM;
    }

    settings.mode = KB_MODE_NORMAL;

#if CONFIG_YKB_SPLIT
    // Left mappings come first in DEFAULT_KEYMAP
#if CONFIG_YKB_LEFT
    memcpy(settings.mappings, DEFAULT_KEYMAP,
           sizeof(kb_map_rule_t) * CONFIG_KB_KEY_COUNT);
#endif // CONFIG_YKB_LEFT
#if CONFIG_YKB_RIGHT
    memcpy(settings.mappings, &DEFAULT_KEYMAP[CONFIG_KB_KEY_COUNT_LEFT],
           sizeof(kb_map_rule_t) * CONFIG_KB_KEY_COUNT)
#endif // CONFIG_YKB_LEFT
#else
    memcpy(settings.mappings, DEFAULT_KEYMAP, sizeof(DEFAULT_KEYMAP));
#endif // CONFIG_YKB_SPLIT

#if CONFIG_BT_INTER_KB_COMM_MASTER
#if CONFIG_YKB_LEFT
        memcpy(settings.mappings_slave,
               &DEFAULT_KEYMAP[CONFIG_KB_KEY_COUNT_LEFT],
               sizeof(kb_map_rule_t) * CONFIG_KB_KEY_COUNT);
#endif // CONFIG_YKB_LEFT
#if CONFIG_YKB_RIGHT
    memcpy(settings.mappings_slave, DEFAULT_KEYMAP,
           sizeof(kb_map_rule_t) * CONFIG_KB_KEY_COUNT);
#endif // CONFIG_YKB_RIGHT
#endif // CONFIG_BT_INTER_KB_COMM_MASTER

    LOG_INF("Loaded keyboard defaults (threshold=%u)",
            (unsigned)((uint16_t)default_threshold));
}

static bool s_loaded_ok = false;

/* -------- settings handler: load path ("kb/blob") -------- */
static int kb_settings_set(const char *key, size_t len,
                           settings_read_cb read_cb, void *cb_arg) {
    if (strcmp(key, KB_SETTINGS_ITEM) != 0) {
        return -ENOENT;
    }

    if (len < sizeof(struct kb_settings_image)) {
        LOG_WRN("kb settings too small (%zu)", len);
        return -EINVAL;
    }

    struct kb_settings_image img;
    ssize_t rlen = read_cb(cb_arg, &img, sizeof(img));
    if (rlen < 0) {
        LOG_ERR("kb settings read_cb error: %d", (int)rlen);
        return (int)rlen;
    }
    if ((size_t)rlen != sizeof(img)) {
        LOG_WRN("kb settings truncated: %zd", rlen);
        return -EINVAL;
    }

    if (img.version != KB_SETTINGS_IMAGE_VERSION) {
        LOG_WRN("kb settings version mismatch: got %u, want %u", img.version,
                KB_SETTINGS_IMAGE_VERSION);
        /* You could attempt migration here if you increment versions later. */
        return -EINVAL;
    }

    settings = img.settings;
    s_loaded_ok = true;
    LOG_INF("Keyboard settings loaded from NVS");
    return 0;
}

/* Optional: enable `settings_save()` flow (not required if you use save_one) */
static int kb_settings_export(int (*export_func)(const char *name,
                                                 const void *val,
                                                 size_t val_len)) {
    struct kb_settings_image img = {
        .version = KB_SETTINGS_IMAGE_VERSION,
        .settings = settings,
    };
    return export_func(KB_SETTINGS_ITEM, &img, sizeof(img));
}

static struct settings_handler kb_settings_handler = {
    .name = KB_SETTINGS_NS,
    .h_set = kb_settings_set,
    .h_export = kb_settings_export, /* not strictly required */
};

/* ------------- Public API -------------- */

int kb_settings_init(void) {
    int err;

    err = settings_subsys_init();
    if (err) {
        LOG_ERR("settings_subsys_init failed: %d", err);
        /* continue; we'll load defaults below if needed */
    }

    err = settings_register(&kb_settings_handler);
    if (err) {
        LOG_ERR("settings_register failed: %d", err);
        /* continue */
    }

    s_loaded_ok = false; /* reset flag */
    err = settings_load_subtree(KB_SETTINGS_NS);
    if (!s_loaded_ok) { /* <-- key point */
        LOG_WRN("No valid keyboard settings found (err=%d) — loading defaults",
                err);
        kb_settings_load_default();

        struct kb_settings_image img = {
            .version = KB_SETTINGS_IMAGE_VERSION,
            .settings = settings,
        };
        int w = settings_save_one(KB_SETTINGS_KEY, &img, sizeof(img));
        if (w) {
            LOG_WRN("Could not save default kb settings: %d", w);
        } else {
            LOG_INF("Default kb settings saved to NVS");
        }
    }

    return 0;
}

kb_settings_t *kb_settings_get(void) {
    return &settings;
}

void kb_settings_save(void) {
    struct kb_settings_image img = {
        .version = KB_SETTINGS_IMAGE_VERSION,
        .settings = settings,
    };
    int err = settings_save_one(KB_SETTINGS_KEY, &img, sizeof(img));
    if (err) {
        LOG_ERR("settings_save_one failed: %d", err);
    } else {
        LOG_INF("Keyboard settings saved");
    }
}
