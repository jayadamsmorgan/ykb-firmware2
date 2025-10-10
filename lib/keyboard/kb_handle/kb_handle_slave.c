#include "kb_handle_common.h"
#include <lib/keyboard/kb_handle.h>

#include <lib/connect/bt_connect.h>
#include <lib/keyboard/kb_settings.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/kscan.h>

static const struct device *const kscan = DEVICE_DT_GET(DT_PATH(kscan));

static uint16_t values[CONFIG_KB_KEY_COUNT] = {0};

static uint32_t prev_down[KB_BITMAP_WORDS] = {0};
static uint32_t curr_down[KB_BITMAP_WORDS] = {0};

static void on_press_slave(uint8_t key_index, kb_settings_t *settings) {
    handle_bl_on_event(key_index, settings, true, values);
}

static void on_release_slave(uint8_t key_index, kb_settings_t *settings) {
    handle_bl_on_event(key_index, settings, false, values);
}

void kb_handle() {

    kb_settings_t *settings = kb_settings_get();

    if (!get_kscan_bitmap(settings, kscan, values, curr_down)) {
        return;
    }

    edge_detection(settings, prev_down, curr_down, KB_BITMAP_BYTECNT,
                   on_press_slave, on_release_slave);

    bt_connect_send_slave_keys(curr_down, KB_BITMAP_BYTECNT);
}
