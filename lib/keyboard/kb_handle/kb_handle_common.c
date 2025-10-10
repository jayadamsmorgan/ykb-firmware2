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
    uint16_t max = settings->maximums[key_index];
    uint16_t min = settings->minimums[key_index];
    double percentage = (double)(values[key_index] - min) / (max - min) * 100;
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

static int64_t last_update_time = 0;

bool get_kscan_bitmap(kb_settings_t *settings, const struct device *const kscan,
                      uint16_t *values, uint32_t *curr_down) {

    int64_t uptime = k_uptime_get();
    int64_t delta = uptime - last_update_time;
    if (delta < settings->key_polling_rate) {
        return false;
    }
    last_update_time = uptime;

    switch (settings->mode) {
    case KB_MODE_NORMAL: {
        int res = kscan_poll_normal(kscan, curr_down, settings->key_thresholds,
                                    values);
        if (res < 0) {
            LOG_ERR("Unable to poll normal (err %d)", res);
            return false;
        }
        break;
    }
    case KB_MODE_RACE: {
        int res =
            kscan_poll_race(kscan, curr_down, settings->key_thresholds, values);
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

static bool fn_pressed = false;
static uint8_t fn_buff[CONFIG_KB_FN_KEYSTROKE_MAX_KEYS] = {0};
static uint8_t fn_buff_size = 0;

static bool layer_select = false;

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

void on_press_default(uint8_t *mappings, uint16_t key_index,
                      kb_settings_t *settings) {

    uint8_t code =
        mappings[settings->layer_index * CONFIG_KB_KEY_COUNT + key_index];

    LOG_DBG("Key with index %d and HID code 0x%X pressed", key_index, code);

    if (code < KEY_FN) {
        if (fn_pressed && fn_buff_size < CONFIG_KB_FN_KEYSTROKE_MAX_KEYS) {
            fn_buff[fn_buff_size++] = code;
            process_fn_buff();
        } else if (layer_select && code >= KEY_1_EXCLAMATION &&
                   code <= KEY_0_RPAREN) {
            uint8_t layer_index = code - KEY_1_EXCLAMATION;
            if (layer_index < settings->layer_count) {
                settings->layer_index = layer_index;
            }
        } else {
            return;
        }

    } else if (code == KEY_FN) {

        fn_pressed = true;
        return;

    } else if (code >= KEY_LAYER_NEXT && settings->layer_count > 1) {

        switch (code) {
        case KEY_LAYER_NEXT:
            settings->layer_index =
                (settings->layer_index + 1) % settings->layer_count;
            break;
        case KEY_LAYER_PREV:
            settings->layer_index =
                (settings->layer_index - 1) % settings->layer_count;
            break;
        case KEY_LAYER_SEL:
            layer_select = true;
            break;
        case KEY_LAYER_1:
            settings->layer_index = 0;
            break;
        case KEY_LAYER_2:
            settings->layer_index = 1;
            break;
        case KEY_LAYER_3:
            if (settings->layer_count >= 3)
                settings->layer_index = 2;
            break;
        default:
            LOG_ERR("Unknown key code 0x%X", code);
            break;
        }
    }
}

void on_release_default(uint8_t *mappings, uint16_t key_index,
                        kb_settings_t *settings) {

    uint8_t code =
        mappings[settings->layer_index * CONFIG_KB_KEY_COUNT + key_index];
    LOG_DBG("Key with index %d and HID code 0x%X released", key_index, code);

    if (code == KEY_LAYER_SEL) {
        layer_select = false;
    } else if (code == KEY_FN) {
        fn_pressed = false;
        fn_buff_size = 0;
    } else if (fn_pressed && fn_buff_size > 0) {
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

static const uint8_t modifier_map[8] = {0x01, 0x02, 0x04, 0x08,
                                        0x10, 0x20, 0x40, 0x80};

void clear_hid_report() {
    memset(report, 0, 8);
    report_size = 0;
}

void build_hid_report_from_bitmap(uint8_t *mappings, kb_settings_t *settings,
                                  uint32_t *curr_down) {
    // Modifiers + up to 6 keys
    for (uint16_t i = 0; i < CONFIG_KB_KEY_COUNT; ++i) {
        if (!bm_test(curr_down, i))
            continue;

        uint8_t code =
            mappings[settings->layer_index * CONFIG_KB_KEY_COUNT + i];

        if (code < KEY_LEFTCONTROL) {
            if (report_size < 6) {
                report[2 + report_size++] = code;
            }
        } else if (code < KEY_FN) {
            report[0] |= modifier_map[code - KEY_LEFTCONTROL];
        }
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
