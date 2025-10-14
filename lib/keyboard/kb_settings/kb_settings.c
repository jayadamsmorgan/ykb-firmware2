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

typedef struct {
    uint8_t count;
    kb_map_rule_t rules[CONFIG_KB_MAX_RULES_PER_KEY];
} kb_ruleset_pod_t;

#define KB_SETTINGS_IMAGE_VERSION 2

struct kb_settings_image {
    uint16_t version;

    uint16_t key_polling_rate;
    uint16_t key_thresholds[CONFIG_KB_KEY_COUNT];
    uint16_t minimums[CONFIG_KB_KEY_COUNT];
    uint16_t maximums[CONFIG_KB_KEY_COUNT];
    uint8_t mode;

    kb_ruleset_pod_t mappings[CONFIG_KB_KEY_COUNT];
#if CONFIG_BT_INTER_KB_COMM_MASTER
    kb_ruleset_pod_t mappings_slave[CONFIG_KB_KEY_COUNT_SLAVE];
#endif
};

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
static void build_image_from_runtime(struct kb_settings_image *img) {
    memset(img, 0, sizeof(*img));
    img->version = KB_SETTINGS_IMAGE_VERSION;

    img->key_polling_rate = settings.key_polling_rate;
    img->mode = settings.mode;

    memcpy(img->key_thresholds, settings.key_thresholds,
           sizeof(img->key_thresholds));
    memcpy(img->minimums, settings.minimums, sizeof(img->minimums));
    memcpy(img->maximums, settings.maximums, sizeof(img->maximums));

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
#endif
}

static int kb_settings_set(const char *key, size_t len,
                           settings_read_cb read_cb, void *cb_arg) {
    if (strcmp(key, KB_SETTINGS_ITEM) != 0) {
        return -ENOENT;
    }

    const size_t kb_settings_image_size = sizeof(struct kb_settings_image);
    if (len != kb_settings_image_size) {
        LOG_WRN("kb settings image size mismatch: got %zu, want %zu)", len,
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
        LOG_WRN("kb settings truncated: %zd", rlen);
        return -EINVAL;
    }

    if (img.version != KB_SETTINGS_IMAGE_VERSION) {
        LOG_WRN("kb settings version mismatch: got %u, want %u", img.version,
                KB_SETTINGS_IMAGE_VERSION);
        return -EINVAL;
    }

    // Scalars
    settings.key_polling_rate = img.key_polling_rate;
    settings.mode = img.mode;

    memcpy(settings.key_thresholds, img.key_thresholds,
           sizeof(settings.key_thresholds));
    memcpy(settings.minimums, img.minimums, sizeof(settings.minimums));
    memcpy(settings.maximums, img.maximums, sizeof(settings.maximums));

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
    LOG_INF("Keyboard settings (incl. mappings) loaded from NVS");
    return 0;
}

static int kb_settings_export(int (*export_func)(const char *name,
                                                 const void *val,
                                                 size_t val_len)) {
    struct kb_settings_image img;
    build_image_from_runtime(&img);
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

        // Persist defaults so subsequent boots load from NVS
        struct kb_settings_image img;
        build_image_from_runtime(&img);

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

void kb_settings_factory_reset(void) {
    settings_delete(KB_SETTINGS_NS);
    settings_save_subtree(KB_SETTINGS_NS);
    kb_settings_load_default();
}

void kb_settings_save(void) {
    struct kb_settings_image img;
    build_image_from_runtime(&img);

    int err = settings_save_one(KB_SETTINGS_KEY, &img, sizeof(img));
    if (err) {
        LOG_ERR("settings_save_one failed: %d", err);
    } else {
        LOG_INF("Keyboard settings saved");
    }
}
