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

K_SEM_DEFINE(settings_sem, 0, 1);
static kb_settings_t settings = {0};

/*
 * Namespace: "kb"
 * Item key : "blob"
 * Full path: "kb/blob"
 */
#define KB_SETTINGS_NS "kb"
#define KB_SETTINGS_ITEM "blob"
#define KB_SETTINGS_KEY KB_SETTINGS_NS "/" KB_SETTINGS_ITEM

static kb_ruleset_pod_t runtime_mappings[CONFIG_KB_KEY_COUNT] = {0};

#if CONFIG_BT_INTER_KB_COMM_MASTER
static kb_ruleset_pod_t runtime_mappings_slave[CONFIG_KB_KEY_COUNT_SLAVE] = {0};
#endif // CONFIG_BT_INTER_KB_COMM_MASTER

static bool s_loaded_ok = false;

static on_settings_update_cb on_settings_update = NULL;

void kb_settings_set_on_update(on_settings_update_cb cb) {
    on_settings_update = cb;
}

static void
kb_settings_load_default_keys_calibration(kb_settings_key_calib_t *calibrations,
                                          size_t key_count) {
    uint16_t max = CONFIG_KB_SETTINGS_DEFAULT_MAXIMUM;
    uint16_t min = CONFIG_KB_SETTINGS_DEFAULT_MINIMUM;
    double threshold =
        ((double)(max - min) * CONFIG_KB_SETTINGS_DEFAULT_THRESHOLD / 100) +
        min;
    for (size_t i = 0; i < key_count; ++i) {
        kb_settings_key_calib_t *c = &calibrations[i];
        c->maximum = CONFIG_KB_SETTINGS_DEFAULT_MAXIMUM;
        c->minimum = CONFIG_KB_SETTINGS_DEFAULT_MINIMUM;
        c->threshold = (uint16_t)threshold;
    }
}

static void kb_key_rules_into_kb_ruleset_pods(const kb_key_rules_t *rules,
                                              size_t key_count,
                                              kb_ruleset_pod_t *pods) {
    for (size_t i = 0; i < key_count; ++i) {
        kb_ruleset_pod_t *pod = &pods[i];
        pod->count = rules[i].count;
        size_t offset = 0;
        if (pod->count > CONFIG_KB_MAX_RULES_PER_KEY) {
            LOG_WRN(
                "Rule count for key with index %d exceeds "
                "CONFIG_KB_MAX_RULES_PER_KEY (%d), loading last %d rules...",
                i, CONFIG_KB_MAX_RULES_PER_KEY, CONFIG_KB_MAX_RULES_PER_KEY);
            offset = pod->count - CONFIG_KB_MAX_RULES_PER_KEY;
            pod->count = CONFIG_KB_MAX_RULES_PER_KEY;
        }
        for (size_t index = 0; index < pod->count; ++offset, ++index) {
            pod->rules[index] = rules[i].rules[offset];
        }
    }
}

static void kb_ruleset_pods_into_runtime_pods(kb_ruleset_pod_t *pods,
                                              size_t key_count,
                                              kb_ruleset_pod_t *runtime_pods) {
    for (size_t i = 0; i < key_count; ++i) {
        kb_ruleset_pod_t *pod = &runtime_pods[i];
        pod->count = pods[i].count;
        size_t offset = 0;
        if (pod->count > CONFIG_KB_MAX_RULES_PER_KEY) {
            LOG_WRN(
                "Rule count for key with index %d exceeds "
                "CONFIG_KB_MAX_RULES_PER_KEY (%d), loading last %d rules...",
                i, CONFIG_KB_MAX_RULES_PER_KEY, CONFIG_KB_MAX_RULES_PER_KEY);
            offset = pod->count - CONFIG_KB_MAX_RULES_PER_KEY;
            pod->count = CONFIG_KB_MAX_RULES_PER_KEY;
        }
        for (size_t index = 0; index < pod->count; ++offset, ++index) {
            pod->rules[index] = pods[i].rules[offset];
        }
    }
}

static void kb_settings_keymap_rehydrate(kb_key_rules_t *keymap,
                                         kb_ruleset_pod_t *runtime_keymap,
                                         size_t key_count) {
    for (size_t i = 0; i < key_count; ++i) {
        keymap[i].rules = runtime_keymap[i].rules;
        keymap[i].count = runtime_keymap[i].count;
    }
}

static void kb_settings_load_default() {
    settings.main.key_polling_rate = CONFIG_KB_SETTINGS_DEFAULT_POLLING_RATE;
    settings.main.mode = KB_MODE_NORMAL;

    LOG_DBG("Selected key polling rate: %d", settings.main.key_polling_rate);
    LOG_DBG("Selected mode: %d", settings.main.mode);

    kb_settings_load_default_keys_calibration(settings.keys_calibration,
                                              CONFIG_KB_KEY_COUNT);
    LOG_DBG("Keys calibration values: min: %d, max: %d, thr: %d",
            settings.keys_calibration[0].minimum,
            settings.keys_calibration[0].maximum,
            settings.keys_calibration[0].threshold);

#if CONFIG_BT_INTER_KB_COMM_MASTER
    kb_settings_load_default_keys_calibration(settings.keys_calibration_slave,
                                              CONFIG_KB_KEY_COUNT_SLAVE);
    LOG_DBG("Slave keys calibration values: min: %d, max: %d, thr: %d",
            settings.keys_calibration_slave[0].minimum,
            settings.keys_calibration_slave[0].maximum,
            settings.keys_calibration_slave[0].threshold);
#endif // CONFIG_BT_INTER_KB_COMM_MASTER

    LOG_DBG("Loading default keymap...");
#if CONFIG_YKB_RIGHT
    kb_key_rules_into_kb_ruleset_pods(&DEFAULT_KEYMAP[CONFIG_KB_KEY_COUNT_LEFT],
                                      CONFIG_KB_KEY_COUNT, runtime_mappings);
#else
    kb_key_rules_into_kb_ruleset_pods(DEFAULT_KEYMAP, CONFIG_KB_KEY_COUNT,
                                      runtime_mappings);
#endif // CONFIG_YKB_RIGHT
    kb_settings_keymap_rehydrate(settings.mappings, runtime_mappings,
                                 CONFIG_KB_KEY_COUNT);
    LOG_DBG("Loaded default keymap.");

#if CONFIG_BT_INTER_KB_COMM_MASTER
    LOG_DBG("Loading default slave keymap...");
#if CONFIG_YKB_RIGHT
    kb_key_rules_into_kb_ruleset_pods(DEFAULT_KEYMAP, CONFIG_KB_KEY_COUNT_SLAVE,
                                      runtime_mappings_slave);
#endif // CONFIG_YKB_RIGHT
#if CONFIG_YKB_LEFT
    kb_key_rules_into_kb_ruleset_pods(&DEFAULT_KEYMAP[CONFIG_KB_KEY_COUNT_LEFT],
                                      CONFIG_KB_KEY_COUNT_SLAVE,
                                      runtime_mappings_slave);
#endif // CONFIG_YKB_LEFT
    kb_settings_keymap_rehydrate(settings.mappings_slave,
                                 runtime_mappings_slave,
                                 CONFIG_KB_KEY_COUNT_SLAVE);
    LOG_DBG("Loaded default slave keymap.");
#endif // CONFIG_BT_INTER_KB_COMM_MASTER

    if (on_settings_update) {
        on_settings_update(&settings);
    }
}

void kb_settings_build_image_from_runtime(struct kb_settings_image *img) {
    img->version = KB_SETTINGS_IMAGE_VERSION;
    img->main = settings.main;

    memcpy(img->keys_calibration, settings.keys_calibration,
           sizeof(img->keys_calibration));
    kb_key_rules_into_kb_ruleset_pods(settings.mappings, CONFIG_KB_KEY_COUNT,
                                      img->mappings);

#if CONFIG_BT_INTER_KB_COMM_MASTER
    memcpy(img->keys_calibration_slave, settings.keys_calibration_slave,
           sizeof(img->keys_calibration_slave));
    kb_key_rules_into_kb_ruleset_pods(settings.mappings_slave,
                                      CONFIG_KB_KEY_COUNT_SLAVE,
                                      img->mappings_slave);
#endif // CONFIG_BT_INTER_KB_COMM_MASTER
}

kb_settings_t *kb_settings_get() {
    return &settings;
}

void kb_settings_save() {
    struct kb_settings_image img;
    kb_settings_build_image_from_runtime(&img);
    int w = settings_save_one(KB_SETTINGS_KEY, &img, sizeof(img));
    if (w) {
        LOG_WRN("Could not save keyboard settings: %d", w);
    } else {
        LOG_INF("Keyboard settings saved.");
    }
}

static void kb_settings_load_from_image(struct kb_settings_image *img) {
    settings.main.mode = img->main.mode;
    settings.main.key_polling_rate = img->main.key_polling_rate;

    for (size_t i = 0; i < CONFIG_KB_KEY_COUNT; ++i) {
        settings.keys_calibration[i] = img->keys_calibration[i];
    }
    kb_ruleset_pods_into_runtime_pods(img->mappings, CONFIG_KB_KEY_COUNT,
                                      runtime_mappings);
    kb_settings_keymap_rehydrate(settings.mappings, runtime_mappings,
                                 CONFIG_KB_KEY_COUNT);

#if CONFIG_BT_INTER_KB_COMM_MASTER
    for (size_t i = 0; i < CONFIG_KB_KEY_COUNT_SLAVE; ++i) {
        settings.keys_calibration_slave[i] = img->keys_calibration_slave[i];
    }
    kb_ruleset_pods_into_runtime_pods(
        img->mappings_slave, CONFIG_KB_KEY_COUNT_SLAVE, runtime_mappings_slave);
    kb_settings_keymap_rehydrate(settings.mappings_slave,
                                 runtime_mappings_slave,
                                 CONFIG_KB_KEY_COUNT_SLAVE);
#endif // CONFIG_BT_INTER_KB_COMM_MASTER
}

int kb_settings_handler_set(const char *key, size_t len,
                            settings_read_cb read_cb, void *cb_arg) {
    if (strcmp(key, KB_SETTINGS_ITEM) != 0) {
        return -ENOENT;
    }

    const size_t kb_settings_img_size = sizeof(struct kb_settings_image);
    if (len != kb_settings_img_size) {
        LOG_ERR("Keyboard settings image size mismatch: got %zu, want %zu", len,
                kb_settings_img_size);
        return -EINVAL;
    }

    struct kb_settings_image img;
    ssize_t rlen = read_cb(cb_arg, &img, sizeof(img));

    if (rlen < 0) {
        LOG_ERR("Keyboard settings read_cb error: %d", (int)rlen);
        return -EINVAL;
    }

    if ((size_t)rlen != sizeof(img)) {
        LOG_ERR("Keyboard settings truncated: %zd", rlen);
        return -EINVAL;
    }

    if (img.version != KB_SETTINGS_IMAGE_VERSION) {
        LOG_ERR("Keyboad settings image version mismatch: got %u, want %u",
                img.version, KB_SETTINGS_IMAGE_VERSION);
        return -EINVAL;
    }

    kb_settings_load_from_image(&img);

    kb_settings_keymap_rehydrate(settings.mappings, runtime_mappings,
                                 CONFIG_KB_KEY_COUNT);

#if CONFIG_BT_INTER_KB_COMM_MASTER
    kb_settings_keymap_rehydrate(settings.mappings_slave,
                                 runtime_mappings_slave,
                                 CONFIG_KB_KEY_COUNT_SLAVE);
#endif // CONFIG_BT_INTER_KB_COMM_MASTER

    if (on_settings_update) {
        on_settings_update(&settings);
    }
    s_loaded_ok = true;
    return 0;
}

int kb_settings_handler_export(int (*export_func)(const char *name,
                                                  const void *val,
                                                  size_t val_len)) {
    struct kb_settings_image img;
    kb_settings_build_image_from_runtime(&img);
    return export_func(KB_SETTINGS_ITEM, &img, sizeof(img));
}

struct settings_handler settings_handler = {
    .name = KB_SETTINGS_NS,
    .h_set = kb_settings_handler_set,
    .h_export = kb_settings_handler_export,
};

int kb_settings_init() {
    int err;

    s_loaded_ok = false;

    err = settings_subsys_init();
    if (err) {
        LOG_ERR("settings_subsys_init err %d", err);
        goto load_defaults;
    }

    err = settings_register(&settings_handler);
    if (err) {
        LOG_ERR("settings_register failed: %d", err);
        goto load_defaults;
    }

    err = settings_load_subtree(KB_SETTINGS_NS);

    if (!s_loaded_ok) {
        LOG_WRN("No valid keyboard settings found (err=%d) — loading defaults",
                err);
        goto load_defaults;
    }

    return 0;

load_defaults:

    kb_settings_load_default();
    kb_settings_save();

    return 0;
}
