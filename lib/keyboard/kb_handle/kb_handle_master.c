#include <lib/keyboard/kb_handle.h>

#include "kb_handle_common.h"
#include <lib/keyboard/kb_handle.h>

#include <lib/connect/bt_connect.h>
#include <lib/keyboard/kb_settings.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/kscan.h>

#include YKB_FN_KEYSTROKES_PATH

static const struct device *const kscan = DEVICE_DT_GET(DT_PATH(kscan));

static uint16_t values[CONFIG_KB_KEY_COUNT] = {0};

static uint32_t prev_down[KB_BITMAP_WORDS];
static uint32_t prev_down_slave[KB_BITMAP_WORDS_SLAVE];

static uint32_t curr_down[KB_BITMAP_WORDS];
static uint32_t curr_down_slave[KB_BITMAP_WORDS_SLAVE];

void on_press_master(uint8_t key_index, kb_settings_t *settings) {
    handle_bl_on_event(key_index, settings, true, values);
    on_press(settings->mappings, key_index, settings);
}

void on_release_master(uint8_t key_index, kb_settings_t *settings) {
    handle_bl_on_event(key_index, settings, true, values);
    on_release(settings->mappings, key_index, settings);
}

void on_press_slave(uint8_t key_index, kb_settings_t *settings) {
    on_press(&settings->mappings[CONFIG_KB_KEY_COUNT], key_index, settings);
}

void on_release_slave(uint8_t key_index, kb_settings_t *settings) {
    on_release(&settings->mappings[CONFIG_KB_KEY_COUNT], key_index, settings);
}

void kb_handle() {

    kb_settings_t *settings = kb_settings_get();

    if (!get_kscan_bitmap(settings, kscan, values, curr_down)) {
        return;
    }

    edge_detection(settings, prev_down, curr_down, KB_BITMAP_BYTECNT,
                   on_press_master, on_release_master);

    int res =
        bt_connect_get_slave_keys(curr_down_slave, KB_BITMAP_SLAVE_BYTECNT);
    if (!res) {
        edge_detection(settings, prev_down_slave, curr_down_slave,
                       KB_BITMAP_SLAVE_BYTECNT, on_press_slave,
                       on_release_slave);
    }

    clear_hid_report();

    build_hid_report_from_bitmap(settings->mappings, settings, curr_down);
    build_hid_report_from_bitmap(&settings->mappings[CONFIG_KB_KEY_COUNT],
                                 settings, curr_down_slave);

    handle_hid_report();
}
