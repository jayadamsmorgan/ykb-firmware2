#ifndef LIB_KB_SETTINGS_H_
#define LIB_KB_SETTINGS_H_

#include <lib/keyboard/kb_mappings.h>

#include <zephyr/toolchain.h>

#include <stdint.h>

enum kb_mode {
    KB_MODE_NORMAL = 0,
    KB_MODE_RACE,
};

typedef struct {
    enum kb_mode mode;
    uint16_t key_polling_rate;
} kb_settings_main_t;

typedef struct {
    uint16_t minimum;
    uint16_t maximum;
    uint16_t threshold;
} kb_settings_key_calib_t;

typedef struct {
    uint8_t count;
    kb_map_rule_t rules[CONFIG_KB_MAX_RULES_PER_KEY];
} kb_ruleset_pod_t;

typedef struct {

    kb_settings_main_t main;

    kb_settings_key_calib_t keys_calibration[CONFIG_KB_KEY_COUNT];

    kb_key_rules_t mappings[CONFIG_KB_KEY_COUNT];

#if CONFIG_BT_INTER_KB_COMM_MASTER

    kb_settings_key_calib_t keys_calibration_slave[CONFIG_KB_KEY_COUNT_SLAVE];

    kb_key_rules_t mappings_slave[CONFIG_KB_KEY_COUNT_SLAVE];
#endif // CONFIG_BT_INTER_KB_COMM_MASTER

} kb_settings_t;

struct kb_settings_image {

    uint16_t version;
    kb_settings_main_t main;
    kb_settings_key_calib_t keys_calibration[CONFIG_KB_KEY_COUNT];
    kb_ruleset_pod_t mappings[CONFIG_KB_KEY_COUNT];

#if CONFIG_BT_INTER_KB_COMM_MASTER

    kb_settings_key_calib_t keys_calibration_slave[CONFIG_KB_KEY_COUNT_SLAVE];
    kb_ruleset_pod_t mappings_slave[CONFIG_KB_KEY_COUNT_SLAVE];

#endif
};

// Increment every time kb_settings_image changes
#define KB_SETTINGS_IMAGE_VERSION 3

int kb_settings_init();

kb_settings_t *kb_settings_get();

void kb_settings_save_from_image(struct kb_settings_image *img);
void kb_settings_save();

void kb_settings_load_from_image(struct kb_settings_image *img);

void kb_settings_build_image_from_runtime(struct kb_settings_image *img);

typedef void (*on_settings_update_cb)(kb_settings_t *settings);

void kb_settings_set_on_update(on_settings_update_cb cb);

#endif // LIB_KB_SETTINGS_H_
