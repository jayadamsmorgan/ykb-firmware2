#include <lib/keyboard/kb_handle.h>

#include "kb_handle_common.h"
#include <lib/keyboard/kb_handle.h>

#include <lib/connect/bt_connect.h>
#include <lib/keyboard/kb_settings.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/kscan.h>

// Include FN keystrokes
#include YKB_FN_KEYSTROKES_PATH

LOG_MODULE_DECLARE(kb_handle, CONFIG_KB_HANDLE_LOG_LEVEL);

static const struct device *const kscan = DEVICE_DT_GET(DT_PATH(kscan));

// We store master current values here,
// we might need them somewhere else later
static uint16_t values[CONFIG_KB_KEY_COUNT] = {0};
// Slave current values should be obtained later
// some other way

// Bitmap to store master currently pressed keys
static uint32_t curr_down[KB_BITMAP_WORDS] = {0};
// Bitmap to store master pressed keys on last kb_handle invocation
static uint32_t prev_down[KB_BITMAP_WORDS] = {0};

// Bitmap to store slave currently pressed keys
static uint32_t curr_down_slave[KB_BITMAP_WORDS_SLAVE] = {0};
// Bitmap to store slave pressed keys on last kb_handle invocation
static uint32_t prev_down_slave[KB_BITMAP_WORDS_SLAVE] = {0};

// Runs on every master key just pressed once
void on_press_master(uint8_t key_index, kb_settings_t *settings) {
    // Handle backlight 'on_event' if present
    handle_bl_on_event(key_index, settings, true, values);

    // Handle fn keystrokes and layer switches
    on_press_default(settings->mappings, key_index, settings);
}

// Runs on every master key just release once
void on_release_master(uint8_t key_index, kb_settings_t *settings) {
    // Same logic as in on_press_master above
    handle_bl_on_event(key_index, settings, false, values);
    on_release_default(settings->mappings, key_index, settings);
}

// Runs on every slave key just pressed once
void on_press_slave(uint8_t key_index, kb_settings_t *settings) {

    // Backlight 'on_event' is handled on the slave directly

    // Handle fn keystrokes and layer switches
    on_press_default(&settings->mappings[CONFIG_KB_KEY_COUNT], key_index,
                     settings);
}

// Runs on every slave key just released once
void on_release_slave(uint8_t key_index, kb_settings_t *settings) {
    // Same logic as in on_press_slave above
    on_release_default(&settings->mappings[CONFIG_KB_KEY_COUNT], key_index,
                       settings);
}

void kb_handle() {

    kb_settings_t *settings = kb_settings_get();

    // Fill out master curr_down and values
    if (!get_kscan_bitmap(settings, kscan, values, curr_down)) {
        return;
    }

    // Go through master bitmap and trigger on_press or on_release callbacks
    edge_detection(settings, prev_down, curr_down, KB_BITMAP_BYTECNT,
                   on_press_master, on_release_master);

    // Try to get slave keys if possible
    int res =
        bt_connect_get_slave_keys(curr_down_slave, KB_BITMAP_SLAVE_BYTECNT);
    if (!res) {
        // Got the slave keys, trigger on_press or on_release callbacks
        edge_detection(settings, prev_down_slave, curr_down_slave,
                       KB_BITMAP_SLAVE_BYTECNT, on_press_slave,
                       on_release_slave);
    } else {
        // Unable to get slave keys, clear their bitmap just in case
        memset(curr_down_slave, 0, KB_BITMAP_SLAVE_BYTECNT);
        memset(prev_down_slave, 0, KB_BITMAP_SLAVE_BYTECNT);
    }

    // Clear HID report buffer from the previous iteration
    clear_hid_report();

    // Fill out HID report with master keys
    build_hid_report_from_bitmap(settings->mappings, settings, curr_down);

    // Fill out HID report with slave keys
    build_hid_report_from_bitmap(settings->mappings_slave, settings,
                                 curr_down_slave);

    // Send HID report if possible BT/USB
    handle_hid_report();
}
