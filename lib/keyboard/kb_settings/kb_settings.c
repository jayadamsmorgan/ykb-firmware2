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

static void
kb_settings_load_default_keymap(const kb_key_rules_t *default_keymap,
                                size_t key_count,
                                kb_ruleset_pod_t *runtime_keymap) {
    for (size_t i = 0; i < key_count; ++i) {
        kb_ruleset_pod_t *pod = &runtime_keymap[i];
        pod->count = default_keymap[i].count;
        if (pod->count > CONFIG_KB_MAX_RULES_PER_KEY) {
            LOG_WRN(
                "Rule count for key with index %d exceeds "
                "CONFIG_KB_MAX_RULES_PER_KEY (%d), loading last %d rules...",
                i, CONFIG_KB_MAX_RULES_PER_KEY, CONFIG_KB_MAX_RULES_PER_KEY);
            pod->count = CONFIG_KB_MAX_RULES_PER_KEY;
        }
        for (size_t j = 0; j < pod->count; ++j) {
            pod->rules[j] = default_keymap[i].rules[j];
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
    kb_settings_load_default_keymap(&DEFAULT_KEYMAP[CONFIG_KB_KEY_COUNT_LEFT],
                                    CONFIG_KB_KEY_COUNT, runtime_mappings);
#else
    kb_settings_load_default_keymap(DEFAULT_KEYMAP, CONFIG_KB_KEY_COUNT,
                                    runtime_mappings);
#endif // CONFIG_YKB_RIGHT
    kb_settings_keymap_rehydrate(settings.mappings, runtime_mappings,
                                 CONFIG_KB_KEY_COUNT);
    LOG_DBG("Loaded default keymap.");

#if CONFIG_BT_INTER_KB_COMM_MASTER
    LOG_DBG("Loading default slave keymap...");
#if CONFIG_YKB_RIGHT
    kb_settings_load_default_keymap(DEFAULT_KEYMAP, CONFIG_KB_KEY_COUNT_SLAVE,
                                    runtime_mappings_slave);
#endif // CONFIG_YKB_RIGHT
#if CONFIG_YKB_LEFT
    kb_settings_load_default_keymap(&DEFAULT_KEYMAP[CONFIG_KB_KEY_COUNT_LEFT],
                                    CONFIG_KB_KEY_COUNT_SLAVE,
                                    runtime_mappings_slave);
#endif // CONFIG_YKB_LEFT
    kb_settings_keymap_rehydrate(settings.mappings_slave,
                                 runtime_mappings_slave,
                                 CONFIG_KB_KEY_COUNT_SLAVE);
    LOG_DBG("Loaded default slave keymap.");
#endif // CONFIG_BT_INTER_KB_COMM_MASTER
}

void kb_settings_set_on_update(on_settings_update_cb cb) {
    on_settings_update = cb;
}

void kb_settings_get(kb_settings_t *kb_settings) {
    k_sem_take(&settings_sem, K_FOREVER);
    memcpy(kb_settings, &settings, sizeof(kb_settings_t));
    k_sem_give(&settings_sem);
}

void kb_settings_save() {
    k_sem_take(&settings_sem, K_FOREVER);

    k_sem_give(&settings_sem);
}

static void kb_settings_load_from_image(struct kb_settings_image *img) {
    settings.main.mode = img->main.mode;
    settings.main.key_polling_rate = img->main.key_polling_rate;

    for (size_t i = 0; i < CONFIG_KB_KEY_COUNT; ++i) {
        settings.keys_calibration[i] = img->keys_calibration[i];
    }

#if CONFIG_BT_INTER_KB_COMM_MASTER
    for (size_t i = 0; i < CONFIG_KB_KEY_COUNT_SLAVE; ++i) {
        settings.keys_calibration_slave[i] = img->keys_calibration_slave[i];
    }
#endif // CONFIG_BT_INTER_KB_COMM_MASTER

    for (size_t i = 0; i < CONFIG_KB_KEY_COUNT; ++i) {
        settings.mappings[i].count = img->mappings[i].count;
    }
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

    return 0;
}

int kb_settings_handler_export() {}

struct settings_handler settings_handler = {
    .name = KB_SETTINGS_NS,
    .h_set = kb_settings_handler_set,
    .h_export = kb_settings_handler_export,
};

int kb_settings_init() {

    int err;

    err = settings_subsys_init();
    if (err) {
        LOG_ERR("settings_subsys_init err %d", err);
    }

    err = settings_load_subtree(KB_SETTINGS_NS);

    return 0;
}
