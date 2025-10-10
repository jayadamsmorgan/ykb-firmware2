#ifndef LIB_KB_SETTINGS_H_
#define LIB_KB_SETTINGS_H_

#include <zephyr/toolchain.h>

#include <stdint.h>

enum kb_mode {
    KB_MODE_NORMAL = 0,
    KB_MODE_RACE,
};

typedef struct {
    enum kb_mode mode;

    uint16_t key_polling_rate;

    uint8_t layer_index;
    uint8_t layer_count;

    uint16_t key_thresholds[CONFIG_KB_KEY_COUNT];
    uint16_t minimums[CONFIG_KB_KEY_COUNT];
    uint16_t maximums[CONFIG_KB_KEY_COUNT];

#if CONFIG_BT_INTER_KB_COMM_MASTER
    uint8_t mappings[(CONFIG_KB_KEY_COUNT + CONFIG_KB_KEY_COUNT_SLAVE) *
                     CONFIG_KB_MAX_LAYERS_SUPPORTED];
#else
    uint8_t mappings[CONFIG_KB_KEY_COUNT * CONFIG_KB_MAX_LAYERS_SUPPORTED];
#endif // CONFIG_BT_INTER_KB_COMM_MASTER

} kb_settings_t;

int kb_settings_init();

kb_settings_t *kb_settings_get();

void kb_settings_save();

#endif // LIB_KB_SETTINGS_H_
