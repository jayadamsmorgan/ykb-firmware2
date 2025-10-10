#include "kb_handle_common.h"
#include <lib/keyboard/kb_handle.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/kscan.h>

#include <lib/connect/bt_connect.h>
#include <lib/connect/usb_connect.h>

#include <lib/keyboard/kb_keys.h>
#include <lib/keyboard/kb_settings.h>

// Include FN keystrokes
#include YKB_FN_KEYSTROKES_PATH

LOG_MODULE_DECLARE(kb_handle, CONFIG_KB_HANDLE_LOG_LEVEL);

static const struct device *const kscan = DEVICE_DT_GET(DT_PATH(kscan));

// Bitmap to store currently pressed keys
static uint32_t curr_down[KB_BITMAP_WORDS] = {0};
// Bitmap to store pressed keys on last kb_handle invocation
static uint32_t prev_down[KB_BITMAP_WORDS] = {0};

// We store current values here, we might need them somewhere else later
static uint16_t values[CONFIG_KB_KEY_COUNT] = {0};

// Runs on every key just pressed once
static void on_press(uint8_t key_index, kb_settings_t *settings) {
    // Handle backlight 'on_event' if present
    handle_bl_on_event(key_index, settings, true, values);

    // Handle fn keystrokes and layer switches
    on_press_default(settings->mappings, key_index, settings);
}

// Runs on every key just release once
static void on_release(uint8_t key_index, kb_settings_t *settings) {
    // Same logic as in on_press above
    handle_bl_on_event(key_index, settings, false, values);
    on_release_default(settings->mappings, key_index, settings);
}

void kb_handle() {
    kb_settings_t *settings = kb_settings_get();

    // Fill out curr_down and values
    if (!get_kscan_bitmap(settings, kscan, values, curr_down)) {
        return;
    }

    // Go through bitmap and trigger on_press or on_release callbacks
    edge_detection(settings, prev_down, curr_down, KB_BITMAP_BYTECNT, on_press,
                   on_release);

    // Clear HID report buffer from the previous iteration
    clear_hid_report();

    // Fill out HID report with the keys
    build_hid_report_from_bitmap(settings->mappings, settings, curr_down);

    // Send HID report if possible BT/USB
    handle_hid_report();
}
