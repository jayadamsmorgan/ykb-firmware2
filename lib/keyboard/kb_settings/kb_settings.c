#include <lib/keyboard/kb_settings.h>

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/toolchain.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

LOG_MODULE_REGISTER(kb_settings, CONFIG_KB_SETTINGS_LOG_LEVEL);

#include YKB_DEF_MAPPINGS_PATH

BUILD_ASSERT(CONFIG_KB_SETTINGS_DEFAULT_MINIMUM <
                 CONFIG_KB_SETTINGS_DEFAULT_MAXIMUM,
             "Default value for key not pressed should be less than default "
             "value for key pressed fully.");

/*
 * Namespace: "kb"
 * Item key : "blob"
 * Full path: "kb/blob"
 */
#define KB_SETTINGS_NS "kb"
#define KB_SETTINGS_ITEM "blob"
#define KB_SETTINGS_KEY KB_SETTINGS_NS "/" KB_SETTINGS_ITEM

// ---------- Runtime "owned" storage (no persistence) ----------
// Arrays that actually own rules; views will point here.

static kb_map_rule_t s_rules_master[CONFIG_KB_KEY_COUNT]
                                   [CONFIG_KB_MAX_RULES_PER_KEY];
static uint8_t s_rules_master_cnt[CONFIG_KB_KEY_COUNT];

#if CONFIG_BT_INTER_KB_COMM_MASTER
static kb_map_rule_t s_rules_slave[CONFIG_KB_KEY_COUNT_SLAVE]
                                  [CONFIG_KB_MAX_RULES_PER_KEY];
static uint8_t s_rules_slave_cnt[CONFIG_KB_KEY_COUNT_SLAVE];
#endif

// Live runtime settings; contains *views* (kb_key_rules_t) to owned rules
static kb_settings_t settings;

// Load flag
static bool s_loaded_ok = false;

static on_settings_update_cb on_settings_update = NULL;

void kb_settings_set_on_update(on_settings_update_cb cb) {
    on_settings_update = cb;
}

// Point runtime views to owned arrays
static void rehydrate_views_from_owned(void) {
    for (size_t i = 0; i < CONFIG_KB_KEY_COUNT; ++i) {
        settings.mappings[i].rules = s_rules_master[i];
        settings.mappings[i].count = s_rules_master_cnt[i];
    }
#if CONFIG_BT_INTER_KB_COMM_MASTER
    for (size_t i = 0; i < CONFIG_KB_KEY_COUNT_SLAVE; ++i) {
        settings.mappings_slave[i].rules = s_rules_slave[i];
        settings.mappings_slave[i].count = s_rules_slave_cnt[i];
    }
#endif
}

static void load_default_keymap(void) {

#if CONFIG_YKB_RIGHT
    size_t off = CONFIG_KB_KEY_COUNT_LEFT;
#else
    size_t off = 0;
#endif

    for (size_t i = 0; i < CONFIG_KB_KEY_COUNT; ++i) {
        const kb_key_rules_t *src = &DEFAULT_KEYMAP[off + i];
        uint8_t n = src->count;
        if (n > CONFIG_KB_MAX_RULES_PER_KEY) {
            LOG_WRN("Key %u rule count %u > max %u; clipping", (unsigned)i,
                    (unsigned)n, (unsigned)CONFIG_KB_MAX_RULES_PER_KEY);
            n = CONFIG_KB_MAX_RULES_PER_KEY;
        }
        s_rules_master_cnt[i] = n;
        for (uint8_t j = 0; j < n; ++j) {
            s_rules_master[i][j] = src->rules[j];
        }
    }

#if CONFIG_BT_INTER_KB_COMM_MASTER
#if CONFIG_YKB_LEFT
    off = CONFIG_KB_KEY_COUNT_LEFT;
#elif CONFIG_YKB_RIGHT
    off = 0;
#endif

    for (size_t i = 0; i < CONFIG_KB_KEY_COUNT_SLAVE; ++i) {
        const kb_key_rules_t *src = &DEFAULT_KEYMAP[off + i];
        uint8_t n = src->count;
        if (n > CONFIG_KB_MAX_RULES_PER_KEY) {
            LOG_WRN("Slave key %u rule count %u > max %u; clipping",
                    (unsigned)i, (unsigned)n,
                    (unsigned)CONFIG_KB_MAX_RULES_PER_KEY);
            n = CONFIG_KB_MAX_RULES_PER_KEY;
        }
        s_rules_slave_cnt[i] = n;
        for (uint8_t j = 0; j < n; ++j) {
            s_rules_slave[i][j] = src->rules[j];
        }
    }

#endif

    rehydrate_views_from_owned();
}

// Build a persistable image from current runtime state
void kb_settings_build_image_from_runtime(struct kb_settings_image *img) {
    memset(img, 0, sizeof(*img));
    img->version = KB_SETTINGS_IMAGE_VERSION;

    img->main = settings.main;

    memcpy(img->keys_calibration, settings.keys_calibration,
           sizeof(img->keys_calibration));

    // MASTER rules → POD
    for (size_t i = 0; i < CONFIG_KB_KEY_COUNT; ++i) {
        img->mappings[i].count = s_rules_master_cnt[i];
        for (uint8_t j = 0; j < s_rules_master_cnt[i]; ++j) {
            img->mappings[i].rules[j] = s_rules_master[i][j];
        }
    }

#if CONFIG_BT_INTER_KB_COMM_MASTER
    // SLAVE rules → POD
    for (size_t i = 0; i < CONFIG_KB_KEY_COUNT_SLAVE; ++i) {
        img->mappings_slave[i].count = s_rules_slave_cnt[i];
        for (uint8_t j = 0; j < s_rules_slave_cnt[i]; ++j) {
            img->mappings_slave[i].rules[j] = s_rules_slave[i][j];
        }
    }
    memcpy(img->keys_calibration_slave, settings.keys_calibration_slave,
           sizeof(img->keys_calibration_slave));
#endif
}

static int kb_settings_set(const char *key, size_t len,
                           settings_read_cb read_cb, void *cb_arg) {
    if (strcmp(key, KB_SETTINGS_ITEM) != 0) {
        return -ENOENT;
    }

    const size_t kb_settings_image_size = sizeof(struct kb_settings_image);
    if (len != kb_settings_image_size) {
        LOG_ERR("kb settings image size mismatch: got %zu, want %zu)", len,
                kb_settings_image_size);
        return -EINVAL;
    }

    struct kb_settings_image img;
    ssize_t rlen = read_cb(cb_arg, &img, sizeof(img));
    if (rlen < 0) {
        LOG_ERR("kb settings read_cb error: %d", (int)rlen);
        return (int)rlen;
    }
    if ((size_t)rlen != sizeof(img)) {
        LOG_ERR("kb settings truncated: %zd", rlen);
        return -EINVAL;
    }

    if (img.version != KB_SETTINGS_IMAGE_VERSION) {
        LOG_ERR("kb settings version mismatch: got %u, want %u", img.version,
                KB_SETTINGS_IMAGE_VERSION);
        return -EINVAL;
    }

    // Scalars
    settings.main = img.main;

    memcpy(settings.keys_calibration, img.keys_calibration,
           sizeof(settings.keys_calibration));

    // MASTER rules: POD → owned
    for (size_t i = 0; i < CONFIG_KB_KEY_COUNT; ++i) {
        uint8_t n = img.mappings[i].count;
        if (n > CONFIG_KB_MAX_RULES_PER_KEY) {
            LOG_WRN("Key %u persisted rule count %u > max %u; clipping",
                    (unsigned)i, (unsigned)n,
                    (unsigned)CONFIG_KB_MAX_RULES_PER_KEY);
            n = CONFIG_KB_MAX_RULES_PER_KEY;
        }
        s_rules_master_cnt[i] = n;
        for (uint8_t j = 0; j < n; ++j) {
            s_rules_master[i][j] = img.mappings[i].rules[j];
        }
    }

#if CONFIG_BT_INTER_KB_COMM_MASTER
    memcpy(settings.keys_calibration_slave, img.keys_calibration_slave,
           sizeof(settings.keys_calibration_slave));

    // SLAVE rules: POD → owned
    for (size_t i = 0; i < CONFIG_KB_KEY_COUNT_SLAVE; ++i) {
        uint8_t n = img.mappings_slave[i].count;
        if (n > CONFIG_KB_MAX_RULES_PER_KEY) {
            LOG_WRN("Slave key %u persisted rule count %u > max %u; clipping",
                    (unsigned)i, (unsigned)n,
                    (unsigned)CONFIG_KB_MAX_RULES_PER_KEY);
            n = CONFIG_KB_MAX_RULES_PER_KEY;
        }
        s_rules_slave_cnt[i] = n;
        for (uint8_t j = 0; j < n; ++j) {
            s_rules_slave[i][j] = img.mappings_slave[i].rules[j];
        }
    }
#endif

    rehydrate_views_from_owned();
    s_loaded_ok = true;
    LOG_INF("Keyboard settings (incl. mappings) loaded");

    if (on_settings_update) {
        on_settings_update(&settings);
    }

    return 0;
}

static int kb_settings_export(int (*export_func)(const char *name,
                                                 const void *val,
                                                 size_t val_len)) {
    struct kb_settings_image img;
    kb_settings_build_image_from_runtime(&img);
    return export_func(KB_SETTINGS_ITEM, &img, sizeof(img));
}

static struct settings_handler kb_settings_handler = {
    .name = KB_SETTINGS_NS,
    .h_set = kb_settings_set,
    .h_export = kb_settings_export,
};

// ---------- Defaults (scalars) ----------

static void kb_settings_load_default(void) {
    memset(&settings, 0, sizeof(settings));

    settings.main.key_polling_rate = CONFIG_KB_SETTINGS_DEFAULT_POLLING_RATE;
    settings.main.mode = KB_MODE_NORMAL;

    float range = (float)(CONFIG_KB_SETTINGS_DEFAULT_MAXIMUM -
                          CONFIG_KB_SETTINGS_DEFAULT_MINIMUM);
    float default_threshold =
        ((range / 100.0f) * CONFIG_KB_SETTINGS_DEFAULT_THRESHOLD) +
        (float)CONFIG_KB_SETTINGS_DEFAULT_MINIMUM;

    kb_settings_key_calib_t key_calib = {
        .minimum = CONFIG_KB_SETTINGS_DEFAULT_MINIMUM,
        .maximum = CONFIG_KB_SETTINGS_DEFAULT_MAXIMUM,
        .threshold = (uint16_t)default_threshold,
    };
    for (size_t i = 0; i < CONFIG_KB_KEY_COUNT; ++i) {
        settings.keys_calibration[i] = key_calib;
    }

#if CONFIG_BT_INTER_KB_COMM_MASTER

    for (size_t i = 0; i < CONFIG_KB_KEY_COUNT_SLAVE; ++i) {
        settings.keys_calibration_slave[i] = key_calib;
    }

#endif // CONFIG_BT_INTER_KB_COMM_MASTER

    load_default_keymap();

    LOG_INF("Loaded keyboard defaults (threshold=%u)",
            (unsigned)((uint16_t)default_threshold));
}

// ---------- Public API ----------

int kb_settings_init(void) {
    int err;

    err = settings_subsys_init();
    if (err) {
        LOG_ERR("settings_subsys_init failed: %d", err);
        // continue; we will still try to load/save
    }

    err = settings_register(&kb_settings_handler);
    if (err) {
        LOG_ERR("settings_register failed: %d", err);
        // continue; we can still run with defaults
    }

    s_loaded_ok = false;
    err = settings_load_subtree(KB_SETTINGS_NS);

    if (!s_loaded_ok) {
        LOG_WRN("No valid keyboard settings found (err=%d) — loading defaults",
                err);

        // Load scalar defaults + deep-copy mappings from DEFAULT_KEYMAP
        kb_settings_load_default();
        if (on_settings_update) {
            on_settings_update(&settings);
        }

        // Persist defaults so subsequent boots load from Settings
        struct kb_settings_image img;
        kb_settings_build_image_from_runtime(&img);

        int w = settings_save_one(KB_SETTINGS_KEY, &img, sizeof(img));
        if (w) {
            LOG_WRN("Could not save default kb settings: %d", w);
        } else {
            LOG_INF("Default kb settings saved.");
        }
    }

    return 0;
}

kb_settings_t *kb_settings_get(void) {
    return &settings;
}

void kb_settings_factory_reset(void) {
    settings_delete(KB_SETTINGS_NS);
    settings_save_subtree(KB_SETTINGS_NS);
    kb_settings_load_default();
}

void kb_settings_save(void) {
    struct kb_settings_image img;
    kb_settings_build_image_from_runtime(&img);

    int err = settings_save_one(KB_SETTINGS_KEY, &img, sizeof(img));
    if (err) {
        LOG_ERR("settings_save_one failed: %d", err);
    } else {
        LOG_INF("Keyboard settings saved");
    }
}
