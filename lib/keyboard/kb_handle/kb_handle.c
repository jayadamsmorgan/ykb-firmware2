#include <lib/keyboard/kb_handle.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/kscan.h>

#include <lib/connect/bt_connect.h>
#include <lib/connect/usb_connect.h>

#include <lib/keyboard/kb_keys.h>
#include <lib/keyboard/kb_settings.h>

#include <string.h>

LOG_MODULE_REGISTER(kb_handle, CONFIG_YKB_FIRMWARE_LOG_LEVEL);

static const struct device *const kscan = DEVICE_DT_GET(DT_PATH(kscan));

static int64_t last_update_time = 0;

static uint8_t report[USB_CONNECT_HID_REPORT_COUNT] = {0};
static uint8_t report_size = 0;

#define KB_WORD_BITS 32u
#define KB_BITMAP_WORDS                                                        \
    ((CONFIG_KB_KEY_COUNT + KB_WORD_BITS - 1) / KB_WORD_BITS)
static uint32_t prev_down[KB_BITMAP_WORDS];

static const uint8_t modifier_map[8] = {0x01, 0x02, 0x04, 0x08,
                                        0x10, 0x20, 0x40, 0x80};

static inline void for_each_set_bit(uint32_t word, uint16_t base,
                                    void (*fn)(uint16_t idx, void *ctx),
                                    void *ctx) {
    while (word) {
        uint32_t b = __builtin_ctz(word);
        fn((uint16_t)(base + b), ctx);
        word &= word - 1; // clear lowest set bit
    }
}

static void on_press(uint16_t key_index, void *ctx) {
    kb_settings_t *settings = ctx;
    uint8_t code =
        settings
            ->mappings[settings->layer_index * CONFIG_KB_KEY_COUNT + key_index];

    LOG_DBG("Key with index %d and HID code 0x%X pressed", key_index, code);

    // Edge-triggered special keys:
    if (code == KEY_LAYER && settings->layer_count > 1) {
        settings->layer_index =
            (settings->layer_index + 1) % settings->layer_count;
        return;
    }
    if (code == KEY_FN) {
        // set FN state if you need momentary layer etc.
        return;
    }
}

static void on_release(uint16_t key_index, void *ctx) {
    kb_settings_t *settings = ctx;
    uint8_t code =
        settings
            ->mappings[settings->layer_index * CONFIG_KB_KEY_COUNT + key_index];
    LOG_DBG("Key with index %d and HID code 0x%X released", key_index, code);
    // Edge-triggered release behaviors if you need them (tap dance, etc.)
}

static inline void bm_set(uint32_t *bm, uint16_t idx) {
    bm[idx / KB_WORD_BITS] |= (1u << (idx % KB_WORD_BITS));
}
static inline bool bm_test(const uint32_t *bm, uint16_t idx) {
    return (bm[idx / KB_WORD_BITS] >> (idx % KB_WORD_BITS)) & 1u;
}

static void build_hid_report_from_bitmap(kb_settings_t *settings,
                                         uint32_t curr_down[KB_BITMAP_WORDS]) {
    // Clear report every scan so releases are implicit
    memset(report, 0, sizeof(report));
    report_size = 0;

    // Modifiers + up to 6 keys
    for (uint16_t i = 0; i < CONFIG_KB_KEY_COUNT; ++i) {
        if (!bm_test(curr_down, i))
            continue;

        uint8_t code =
            settings->mappings[settings->layer_index * CONFIG_KB_KEY_COUNT + i];

        if (code < KEY_LEFTCONTROL) {
            if (report_size < 6) {
                report[2 + report_size++] = code;
            }
        } else if (code < KEY_FN) {
            report[0] |= modifier_map[code - KEY_LEFTCONTROL];
        }
    }
}

void kb_handle() {
    kb_settings_t *settings = kb_settings_get();

    int64_t delta = k_uptime_delta(&last_update_time);
    if (delta < settings->key_polling_rate) {
        return;
    }

    uint8_t keys[CONFIG_KSCAN_MAX_SIMULTANIOUS_KEYS] = {0};
    int n = 0;

    switch (settings->mode) {
    case KB_MODE_NORMAL: {
        n = kscan_poll_normal(kscan, keys, settings->key_thresholds);
        if (n < 0) {
            LOG_ERR("Unable to poll normal: %d", n);
            return;
        }
        break;
    }
    case KB_MODE_RACE: {
        int res = kscan_poll_race(kscan, settings->key_thresholds);
        if (res < 0) {
            LOG_ERR("Unable to poll race: %d", res);
            return;
        }
        keys[0] = (uint8_t)res;
        n = 1;
        break;
    }
    }

    uint32_t curr_down[KB_BITMAP_WORDS] = {0};
    for (int i = 0; i < n; ++i) {
        if (keys[i] >= CONFIG_KB_KEY_COUNT)
            continue;
        bm_set(curr_down, keys[i]);
    }

    // Edge detection
    for (size_t w = 0; w < KB_BITMAP_WORDS; ++w) {
        uint32_t base = (uint32_t)(w * KB_WORD_BITS);
        uint32_t presses = curr_down[w] & ~prev_down[w];
        uint32_t releases = prev_down[w] & ~curr_down[w];
        for_each_set_bit(presses, base, on_press, settings);
        for_each_set_bit(releases, base, on_release, settings);
    }

    build_hid_report_from_bitmap(settings, curr_down);

    memcpy(prev_down, curr_down, sizeof(prev_down));

    if (usb_connect_is_ready()) {
        usb_connect_send(report);
    } else if (bt_connect_is_ready()) {
        bt_connect_send(report);
    }
}
