#include "kb_handle_common.h"
#include <lib/keyboard/kb_handle.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/kscan.h>

#include <lib/connect/bt_connect.h>
#include <lib/connect/usb_connect.h>

#include <lib/keyboard/kb_keys.h>
#include <lib/keyboard/kb_settings.h>

#include YKB_FN_KEYSTROKES_PATH

static const struct device *const kscan = DEVICE_DT_GET(DT_PATH(kscan));

static uint32_t curr_down[KB_BITMAP_WORDS] = {0};
static uint32_t prev_down[KB_BITMAP_WORDS] = {0};

static uint16_t values[CONFIG_KB_KEY_COUNT] = {0};

static void on_press_normal(uint8_t key_index, kb_settings_t *settings) {
    handle_bl_on_event(key_index, settings, true, values);
    on_press(settings->mappings, key_index, settings);
}

static void on_release_normal(uint8_t key_index, kb_settings_t *settings) {
    handle_bl_on_event(key_index, settings, false, values);
    on_release(settings->mappings, key_index, settings);
}

void kb_handle() {
    kb_settings_t *settings = kb_settings_get();

    if (!get_kscan_bitmap(settings, kscan, values, curr_down)) {
        return;
    }

    edge_detection(settings, prev_down, curr_down, KB_BITMAP_BYTECNT,
                   on_press_normal, on_release_normal);

    clear_hid_report();

    build_hid_report_from_bitmap(settings->mappings, settings, curr_down);

    handle_hid_report();
}
