#include "kb_handle_common.h"
#include <lib/keyboard/kb_handle.h>

#include <lib/connect/bt_connect.h>
#include <lib/keyboard/kb_settings.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/kscan.h>

// Include predefined keystrokes
// for the currently selected board
#include YKB_FN_KEYSTROKES_PATH

LOG_MODULE_DECLARE(kb_handle, CONFIG_KB_HANDLE_LOG_LEVEL);

static const struct device *const kscan = DEVICE_DT_GET(DT_PATH(kscan));

// We store current values here, we might need them somewhere else later
static uint16_t values[CONFIG_KB_KEY_COUNT] = {0};

// Bitmap to store currently pressed keys
static uint32_t curr_down[KB_BITMAP_WORDS] = {0};
// Bitmap to store pressed keys on last kb_handle invocation
static uint32_t prev_down[KB_BITMAP_WORDS] = {0};

// Runs on every key just pressed once
static void on_press_slave(uint8_t key_index, kb_settings_t *settings) {

    // Handle backlight 'on_event' if present
    handle_bl_on_event(key_index, settings, true, values);

    if (!bt_connect_is_ready()) {
        // If we are not connected to the master
        // we can also check for keystrokes
        // to be able to do something, idk...
        on_press_default(settings->mappings, key_index, settings);
    }
}

// Runs on every key just released once
static void on_release_slave(uint8_t key_index, kb_settings_t *settings) {
    // Same logic as in on_press_slave above
    handle_bl_on_event(key_index, settings, false, values);
    if (!bt_connect_is_ready()) {
        on_release_default(settings->mappings, key_index, settings);
    }
}

void kb_handle() {

    kb_settings_t *settings = kb_settings_get();

    // Fill out curr_down and values
    if (!get_kscan_bitmap(settings, kscan, values, curr_down)) {
        // Either too early to poll, no key pressed or error
        return;
    }

    // Go through bitmap and trigger on_press or on_release callbacks
    edge_detection(settings, prev_down, curr_down, KB_BITMAP_BYTECNT,
                   on_press_slave, on_release_slave);

    // Send keys to the master
    bt_connect_send_slave_keys(curr_down, KB_BITMAP_BYTECNT);

    // It doesn't really make sense to
    // do anything else with them here
    // since master should handle everything
}
