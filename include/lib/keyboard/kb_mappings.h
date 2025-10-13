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
    uint16_t mods_mask;
    uint16_t mods_eq;
    uint8_t out_code;
    bool out_shifted;
} kb_map_rule_t;

typedef struct {
    const kb_map_rule_t *rules;
    uint8_t count;
} kb_key_rules_t;

/* Backward-compatible RULE: mask defaults to 'eq' (exact match only). */
#define RULE(eq, out, shifted)                                                 \
    (kb_map_rule_t) {                                                          \
        .mods_eq = (eq), .mods_mask = (eq), .out_code = (out),                 \
        .out_shifted = (shifted)                                               \
    }

/* Explicit mask version (use this if you ever want eq != mask). */
#define RULE_MASK(eq, mask, out, shifted)                                      \
    (kb_map_rule_t) {                                                          \
        .mods_eq = (eq), .mods_mask = (mask), .out_code = (out),               \
        .out_shifted = (shifted)                                               \
    }

#define RULE_SIMPLE(out) RULE(0, out, false)

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

// Translate one physical key given context.
// Writes result HID code into 'out'
//
// Returns true if matching rule found, false otherwise
static inline bool kb_mapping_translate_key(const kb_key_rules_t *kr,
                                            uint16_t mods_ext, uint8_t *out,
                                            bool *needs_shift) {
    for (uint8_t i = 0; i < kr->count; ++i) {
        const kb_map_rule_t *r = &kr->rules[i];

        /* Start from what the rule says: which bits are “exactly compared”. */
        uint16_t exact_mask = r->mods_mask;
        uint16_t exact_value = r->mods_eq & exact_mask;

        /* Pull out ANY-groups requested by the rule (we treat them as
         * “at least one bit of the group must be pressed”, not equality). */
        uint16_t any_mask = 0;

        if (r->mods_eq & MOD_ANYSHIFT) {
            any_mask |= MOD_ANYSHIFT;
            exact_mask &= ~MOD_ANYSHIFT;
        }
        if (r->mods_eq & MOD_ANYCTRL) {
            any_mask |= MOD_ANYCTRL;
            exact_mask &= ~MOD_ANYCTRL;
        }
        if (r->mods_eq & MOD_ANYALT) {
            any_mask |= MOD_ANYALT;
            exact_mask &= ~MOD_ANYALT;
        }
        if (r->mods_eq & MOD_ANYGUI) {
            any_mask |= MOD_ANYGUI;
            exact_mask &= ~MOD_ANYGUI;
        }
        if (r->mods_eq & MOD_ANYFN) {
            any_mask |= MOD_ANYFN;
            exact_mask &= ~MOD_ANYFN;
        }
        if (r->mods_eq & MOD_ANYLAYER) {
            any_mask |= MOD_ANYLAYER;
            exact_mask &= ~MOD_ANYLAYER;
        }

        /* First, exact bits must match exactly… */
        bool exact_ok = ((mods_ext & exact_mask) == exact_value);

        /* …then, for each ANY-group requested, at least one bit of the group
         * must be present. If no ANY-group is requested, this reduces to true.
         */
        bool any_ok = true;
        if (any_mask) {
            any_ok = (mods_ext & any_mask) != 0;
        }

        if (exact_ok && any_ok) {
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
