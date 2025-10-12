#ifndef KB_MAPPINGS_H_
#define KB_MAPPINGS_H_

#include "stm32wbxx.h"
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

// ---------- Modifier bits ---------- //
enum {
    MOD_NONE = 0,

    MOD_LCTRL = 1u << 0,
    MOD_LSHIFT = 1u << 1,
    MOD_LALT = 1u << 2,
    MOD_LGUI = 1u << 3,
    MOD_RCTRL = 1u << 4,
    MOD_RSHIFT = 1u << 5,
    MOD_RALT = 1u << 6,
    MOD_RGUI = 1u << 7,

    MOD_ANYCTRL = MOD_LCTRL | MOD_RCTRL,
    MOD_ANYSHIFT = MOD_LSHIFT | MOD_RSHIFT,
    MOD_ANYALT = MOD_LALT | MOD_RALT,
    MOD_ANYGUI = MOD_LGUI | MOD_RGUI,

    // Virtual modifiers
    MOD_LFN = 1u << 8,
    MOD_LLAYER = 1u << 9,
    MOD_RFN = 1u << 10,
    MOD_RLAYER = 1u << 11,

    MOD_ANYFN = MOD_LFN | MOD_RFN,
    MOD_ANYLAYER = MOD_LLAYER | MOD_RLAYER,
};

typedef struct {
    uint16_t mods_eq;
    uint8_t out_code;
    bool out_shifted;
} kb_map_rule_t;

typedef struct {
    const kb_map_rule_t *rules;
    uint8_t count;
} kb_key_rules_t;

#define RULE(eq, out, shifted)                                                 \
    (kb_map_rule_t) {                                                          \
        .mods_eq = (eq), .out_code = (out), .out_shifted = (shifted),          \
    }

#define RULES_SIMPLE(out) RULE(MOD_ANYSHIFT, out, true), RULE(0, out, false)

#define KEY(name)                                                              \
    (kb_key_rules_t) {                                                         \
        .rules = __kb_map_rules_key##name,                                     \
        .count = (sizeof(__kb_map_rules_key##name) /                           \
                  sizeof(__kb_map_rules_key##name[0])),                        \
    }

#define RULES_FOR_KEY(name)                                                    \
    static const kb_map_rule_t __kb_map_rules_key##name[] =

#define RULES_MOD_KEY(name, key)                                               \
    RULES_FOR_KEY(name) {                                                      \
        RULE(MOD_NONE, key, false)                                             \
    }

#define DEFAULT_KEYMAP __default_kb_keymap

#define DEFAULT_KEYMAP_DEFINE(...)                                             \
    static const kb_key_rules_t DEFAULT_KEYMAP[KEYMAP_SIZE] = {__VA_ARGS__}

#if CONFIG_YKB_SPLIT
#if CONFIG_YKB_RIGHT
#define GET_MAPPINGS(SETTINGS) SETTINGS->right_mappings
#endif // CONFIG_YKB_RIGHT
#if CONFIG_YKB_LEFT
#define GET_MAPPINGS(SETTINGS) SETTINGS->left_mappings
#endif // CONFIG_YKB_LEFT
#else
#define GET_MAPPINGS(SETTINGS) SETTINGS->mappings
#endif // CONFIG_YKB_SPLIT

// Translate one physical key given context.
// Writes result HID code into 'out'
//
// Returns true if matching rule found, false otherwise
static inline bool kb_mapping_translate_key(const kb_key_rules_t *kr,
                                            uint16_t mods_ext, uint8_t *out,
                                            bool *needs_shift) {
    for (uint8_t i = 0; i < kr->count; ++i) {
        const kb_map_rule_t *r = &kr->rules[i];

        uint16_t mods_eq = r->mods_eq;
        uint16_t mask_bits = r->mods_eq; // which bits to care about

        // Expand "ANY*" sentinels into the actual pressed bits,
        // and make sure we only compare those bits.
        if ((mods_eq & MOD_ANYSHIFT) == MOD_ANYSHIFT) {
            mods_eq = (mods_eq & ~MOD_ANYSHIFT) | (mods_ext & MOD_ANYSHIFT);
            mask_bits = (mask_bits & ~MOD_ANYSHIFT) | MOD_ANYSHIFT;
        }
        if ((mods_eq & MOD_ANYLAYER) == MOD_ANYLAYER) {
            mods_eq = (mods_eq & ~MOD_ANYLAYER) | (mods_ext & MOD_ANYLAYER);
            mask_bits = (mask_bits & ~MOD_ANYLAYER) | MOD_ANYLAYER;
        }
        if ((mods_eq & MOD_ANYALT) == MOD_ANYALT) {
            mods_eq = (mods_eq & ~MOD_ANYALT) | (mods_ext & MOD_ANYALT);
            mask_bits = (mask_bits & ~MOD_ANYALT) | MOD_ANYALT;
        }
        if ((mods_eq & MOD_ANYCTRL) == MOD_ANYCTRL) {
            mods_eq = (mods_eq & ~MOD_ANYCTRL) | (mods_ext & MOD_ANYCTRL);
            mask_bits = (mask_bits & ~MOD_ANYCTRL) | MOD_ANYCTRL;
        }
        if ((mods_eq & MOD_ANYFN) == MOD_ANYFN) {
            mods_eq = (mods_eq & ~MOD_ANYFN) | (mods_ext & MOD_ANYFN);
            mask_bits = (mask_bits & ~MOD_ANYFN) | MOD_ANYFN;
        }
        if ((mods_eq & MOD_ANYGUI) == MOD_ANYGUI) {
            mods_eq = (mods_eq & ~MOD_ANYGUI) | (mods_ext & MOD_ANYGUI);
            mask_bits = (mask_bits & ~MOD_ANYGUI) | MOD_ANYGUI;
        }

        if ((mods_ext & mask_bits) == mods_eq) {
            *out = r->out_code;
            *needs_shift = r->out_shifted;
            return true;
        }
    }

    *out = KEY_NOKEY;
    *needs_shift = false;
    return false;
}

#endif // KB_MAPPINGS_H_
