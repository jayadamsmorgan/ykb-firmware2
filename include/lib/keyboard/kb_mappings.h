#ifndef KB_MAPPINGS_H_
#define KB_MAPPINGS_H_

#include <lib/keyboard/kb_keys.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if CONFIG_YKB_SPLIT

#if CONFIG_YKB_LEFT
#define CONFIG_KB_KEY_COUNT CONFIG_KB_KEY_COUNT_LEFT
#if CONFIG_BT_INTER_KB_COMM_MASTER
#define CONFIG_KB_KEY_COUNT_SLAVE CONFIG_KB_KEY_COUNT_RIGHT
#endif // CONFIG_BT_INTER_KB_COMM_MASTER
#endif // CONFIG_YKB_LEFT

#if CONFIG_YKB_RIGHT
#define CONFIG_KB_KEY_COUNT CONFIG_KB_KEY_COUNT_RIGHT
#if CONFIG_BT_INTER_KB_COMM_MASTER
#define CONFIG_KB_KEY_COUNT_SLAVE CONFIG_KB_KEY_COUNT_LEFT
#endif // CONFIG_BT_INTER_KB_COMM_MASTER
#endif // CONFIG_YKB_RIGHT

#define KEYMAP_SIZE (CONFIG_KB_KEY_COUNT_LEFT + CONFIG_KB_KEY_COUNT_RIGHT)

#else

#define KEYMAP_SIZE CONFIG_KB_KEY_COUNT

#endif // CONFIG_YKB_SPLIT

enum {
    LAYER0 = 0u,
    LAYER1 = 1u << 0,
    LAYER2 = 1u << 1,
    LAYER3 = 1u << 2,

    LAYERS_CNT = 4u,
};

#define LAYER_BTN UINT16_MAX

typedef struct {
    uint16_t layer_eq;
    uint8_t out_code;
} kb_map_rule_t;

typedef struct {
    const kb_map_rule_t *rules;
    uint8_t count;
} kb_key_rules_t;

#define RULE(eq, out)                                                          \
    (kb_map_rule_t) {                                                          \
        .layer_eq = (eq), .out_code = (out),                                   \
    }

#define KEY(name)                                                              \
    (kb_key_rules_t) {                                                         \
        .rules = __kb_map_rules_key##name,                                     \
        .count = (sizeof(__kb_map_rules_key##name) /                           \
                  sizeof(__kb_map_rules_key##name[0])),                        \
    }

#define RULES_FOR_KEY(name)                                                    \
    static const kb_map_rule_t __kb_map_rules_key##name[]

#define RULES_FOR_SIMPLE_KEY(name, out)                                        \
    RULES_FOR_KEY(name) = {                                                    \
        RULE(LAYER0, out),                                                     \
    }

#define RULES_LAYER_KEY(name, out)                                             \
    RULES_FOR_KEY(name) = {                                                    \
        RULE(LAYER_BTN, out),                                                  \
    }

#define DEFAULT_KEYMAP __default_kb_keymap

#define DEFAULT_KEYMAP_DEFINE(...)                                             \
    static const kb_key_rules_t DEFAULT_KEYMAP[KEYMAP_SIZE] = {__VA_ARGS__}

// Translate one physical key given context.
// Writes result HID code into 'out'
//
// Returns true if matching rule found, false otherwise
static inline bool kb_mapping_translate_key(const kb_key_rules_t *kr,
                                            uint16_t mods_ext, uint8_t *out,
                                            bool *needs_shift) {

    if (kr->count == 1 && kr->rules[0].layer_eq == LAYER_BTN) {
        *out = kr->rules[0].out_code;
        return true;
    }

    for (uint8_t i = 0; i < kr->count; ++i) {
        const kb_map_rule_t *r = &kr->rules[i];
        if (mods_ext == r->layer_eq) {
            *out = r->out_code;
            return true;
        }
    }

    *out = KEY_NOKEY;
    *needs_shift = false;
    return false;
}

#endif // KB_MAPPINGS_H_
