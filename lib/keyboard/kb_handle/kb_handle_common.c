#include "kb_handle_common.h"

#include <lib/keyboard/kb_fn_keystroke.h>
#include <lib/keyboard/kb_handle.h>
#include <lib/keyboard/kb_keys.h>
#include <lib/keyboard/kb_settings.h>

#include <drivers/kscan.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <stddef.h>
#include <stdint.h>

LOG_MODULE_REGISTER(kb_handle, CONFIG_KB_HANDLE_LOG_LEVEL);

static inline void for_each_set_bit(uint32_t word, uint16_t base,
                                    key_state_changed_cb cb,
                                    kb_settings_t *settings) {
    while (word) {
        uint32_t b = __builtin_ctz(word);
        cb((uint8_t)(base + b), settings);
        word &= word - 1;
    }
}

static inline uint8_t key_percentage(kb_settings_t *settings, uint16_t *values,
                                     uint8_t key_index) {
    kb_settings_key_calib_t calib = settings->keys_calibration[key_index];
    double percentage = (double)(values[key_index] - calib.minimum) /
                        (calib.maximum - calib.minimum) * 100;
    return (uint8_t)percentage;
}

static inline bool bm_test(const uint32_t *bm, uint16_t idx) {
    return (bm[idx / KB_WORD_BITS] >> (idx % KB_WORD_BITS)) & 1u;
}

void edge_detection(kb_settings_t *settings, uint32_t *prev_down,
                    uint32_t *curr_down, size_t bm_size,
                    key_state_changed_cb on_press,
                    key_state_changed_cb on_release) {
    for (size_t w = 0; w < KB_BITMAP_WORDS; ++w) {
        uint32_t base = (uint32_t)(w * KB_WORD_BITS);
        uint32_t presses = curr_down[w] & ~prev_down[w];
        uint32_t releases = prev_down[w] & ~curr_down[w];
        for_each_set_bit(presses, base, on_press, settings);
        for_each_set_bit(releases, base, on_release, settings);
    }
    memcpy(prev_down, curr_down, bm_size);
}

static uint16_t key_thresholds[CONFIG_KB_KEY_COUNT] = {0};

static void on_settings_update(kb_settings_t *settings) {
    for (size_t i = 0; i < CONFIG_KB_KEY_COUNT; ++i) {
        key_thresholds[i] = settings->keys_calibration[i].threshold;
    }
}

static int64_t last_update_time = 0;

bool get_kscan_bitmap(kb_settings_t *settings, const struct device *const kscan,
                      uint16_t *values, uint32_t *curr_down) {

    int64_t uptime = k_uptime_get();
    int64_t delta = uptime - last_update_time;
    if (delta < settings->main.key_polling_rate) {
        return false;
    }
    last_update_time = uptime;
    memset(curr_down, 0, KB_BITMAP_BYTECNT);

    switch (settings->main.mode) {
    case KB_MODE_NORMAL: {
        int res = kscan_poll_normal(kscan, curr_down, key_thresholds, values);
        if (res < 0) {
            LOG_ERR("Unable to poll normal (err %d)", res);
            return false;
        }
        break;
    }
    case KB_MODE_RACE: {
        int res = kscan_poll_race(kscan, curr_down, key_thresholds, values);
        if (res == -1)
            return false;
        if (res < -1) {
            LOG_ERR("Unable to poll race (err %d)", res);
            return false;
        }
        break;
    }
    }

    return true;
}

static uint8_t fn_buff[CONFIG_KB_FN_KEYSTROKE_MAX_KEYS] = {0};
static uint8_t fn_buff_size = 0;

static void process_fn_buff() {
    if (fn_buff_size == 0)
        return;

    STRUCT_SECTION_FOREACH(kb_fn_keystroke, keystroke) {

        if (keystroke->count != fn_buff_size)
            continue;

        if (strncmp(keystroke->keys, fn_buff, fn_buff_size) == 0) {
            LOG_DBG("Found matching keystroke %s", keystroke->name);
            if (keystroke->cb)
                keystroke->cb();
        }
    }
}

static const uint16_t modifier_map[] = {
    MOD_LCTRL, MOD_LSHIFT, MOD_LALT, MOD_LGUI,   MOD_RCTRL, MOD_RSHIFT,
    MOD_RALT,  MOD_RGUI,   MOD_LFN,  MOD_LLAYER, MOD_RFN,   MOD_RLAYER,
};

static const char *modifier_to_str_map[] = {
    "MOD_LCTRL", "MOD_LSHIFT", "MOD_LALT", "MOD_LGUI",
    "MOD_RCTRL", "MOD_RSHIFT", "MOD_RALT", "MOD_RGUI",
    "MOD_LFN",   "MOD_LLAYER", "MOD_RFN",  "MOD_RLAYER",
};

static uint16_t current_modifiers = 0;

static bool fn_pressed() {
    return ((current_modifiers & MOD_LFN) == MOD_LFN) ||
           ((current_modifiers & MOD_RFN) == MOD_RFN);
};

void on_press_default(kb_key_rules_t *mappings, uint16_t key_index,
                      kb_settings_t *settings) {

    uint8_t code;
    bool needs_shift;
    if (!kb_mapping_translate_key(&mappings[key_index], current_modifiers,
                                  &code, &needs_shift)) {
        LOG_DBG("No rule found for key with index %d", key_index);
        return;
    }

    if (code >= KEY_LEFTCONTROL && code <= KEY_RIGHTLAYER) {

        if (code == KEY_LEFTFN || code == KEY_RIGHTFN)
            fn_buff_size = 0;

        current_modifiers |= modifier_map[code - KEY_LEFTCONTROL];
        LOG_DBG("Key with index %d ('%s') pressed", key_index,
                modifier_to_str_map[code - KEY_LEFTCONTROL]);

        return;
    }

    LOG_DBG("Key with index %d and HID code 0x%X pressed", key_index, code);

    if (code < KEY_LEFTCONTROL && fn_pressed() &&
        fn_buff_size < CONFIG_KB_FN_KEYSTROKE_MAX_KEYS) {
        fn_buff[fn_buff_size++] = code;
        process_fn_buff();
        return;
    }
}

void on_release_default(kb_key_rules_t *mappings, uint16_t key_index,
                        kb_settings_t *settings) {

    uint8_t code;
    bool needs_shift;
    if (!kb_mapping_translate_key(&mappings[key_index], current_modifiers,
                                  &code, &needs_shift)) {
        LOG_DBG("No rule found for key with index %d", key_index);
        return;
    }

    if (code >= KEY_LEFTCONTROL && code <= KEY_RIGHTLAYER) {
        current_modifiers &= ~modifier_map[code - KEY_LEFTCONTROL];
        LOG_DBG("Key with index %d ('%s') pressed", key_index,
                modifier_to_str_map[code - KEY_LEFTCONTROL]);
        return;
    }

    LOG_DBG("Key with index %d and HID code 0x%X released", key_index, code);

    if (fn_pressed() && fn_buff_size > 0) {
        uint8_t i = 0;
        for (i = 0; i < fn_buff_size; ++i) {
            if (fn_buff[i] == code) {
                fn_buff[i] = 0;
                break;
            }
        }
        for (uint8_t j = i + 1; j < fn_buff_size; ++j) {
            fn_buff[j - 1] = fn_buff[j];
        }
        fn_buff_size--;
    }
}

void handle_bl_on_event(uint8_t key_index, kb_settings_t *settings,
                        bool pressed, uint16_t *values) {
#if CONFIG_KB_BACKLIGHT
    kb_key_t key = {
        .index = key_index,
        .pressed = pressed,
        .value = key_percentage(settings, values, key_index),
    };
    kb_backlight_on_event(&key);
#endif // CONFIG_KB_BACKLIGHT
}

static uint8_t report[8];
static uint8_t report_size = 0;

void clear_hid_report() {
    memset(report, 0, 8);
    report_size = 0;
}

void build_hid_report_from_bitmap(kb_key_rules_t *mappings,
                                  kb_settings_t *settings,
                                  uint32_t *curr_down) {
    // Modifiers + up to 6 keys
    memcpy(report, &current_modifiers, 1);
    for (uint16_t i = 0; i < CONFIG_KB_KEY_COUNT; ++i) {

        if (report_size == 6)
            return;

        if (!bm_test(curr_down, i))
            continue;

        uint8_t code;
        bool needs_shift;
        if (!kb_mapping_translate_key(&mappings[i], current_modifiers, &code,
                                      &needs_shift)) {
            continue;
        };
        if (code >= KEY_LEFTCONTROL)
            continue;

        if (needs_shift && ((current_modifiers & MOD_ANYSHIFT) == 0)) {
            report[0] |= MOD_LSHIFT;
        }

        if (report_size < 6)
            report[2 + report_size++] = code;
    }
}

void handle_hid_report() {

#if CONFIG_KB_HANDLE_REPORT_PRIO_USB
    if (usb_connect_is_ready()) {
        usb_connect_handle_wakeup();
        usb_connect_send(report);
    } else if (bt_connect_is_ready()) {
        bt_connect_send(report, report_size);
    }
#elif CONFIG_KB_HANDLE_REPORT_PRIO_BT
    if (bt_connect_is_ready()) {
        bt_connect_send(report, report_size);
    } else if (usb_connect_is_ready()) {
        usb_connect_handle_wakeup();
        usb_connect_send(report);
    }
#elif CONFIG_LIB_BT_CONNECT
    if (bt_connect_is_ready()) {
        bt_connect_send(report, report_size);
    }
#elif CONFIG_LIB_USB_CONNECT
    if (usb_connect_is_ready()) {
        usb_connect_handle_wakeup();
        usb_connect_send(report);
    }
#endif // CONFIG_KB_HANDLE_REPORT_PRIO_USB
}

int kb_handle_init() {

    kb_settings_set_on_update(on_settings_update);

    return 0;
}
