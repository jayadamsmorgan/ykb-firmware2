// SPDX-License-Identifier: Apache-2.0

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/kscan.h>
#include <drivers/mux.h>

#include <zephyr/usb/class/usbd_hid.h>
#include <zephyr/usb/usbd.h>

#include <zephyr/settings/settings.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>

#include "hog.h"
#include <lib/usb_connect.h>

#include <app_version.h>

LOG_MODULE_REGISTER(main, CONFIG_YKB_FIRMWARE_LOG_LEVEL);

// static uint8_t usb_report[USB_CONNECT_HID_REPORT_COUNT];
//
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL),
                  BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)),
};

static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
            sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void connected(struct bt_conn *conn, uint8_t err) {
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (err) {
        printk("Failed to connect to %s, err 0x%02x %s\n", addr, err,
               bt_hci_err_to_str(err));
        return;
    }

    printk("Connected %s\n", addr);

    if (bt_conn_set_security(conn, BT_SECURITY_L2)) {
        printk("Failed to set security\n");
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason) {
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    printk("Disconnected from %s, reason 0x%02x %s\n", addr, reason,
           bt_hci_err_to_str(reason));
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
                             enum bt_security_err err) {
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (!err) {
        printk("Security changed: %s level %u\n", addr, level);
    } else {
        printk("Security failed: %s level %u err %s(%d)\n", addr, level,
               bt_security_err_to_str(err), err);
    }
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .security_changed = security_changed,
};

static void bt_ready(int err) {
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return;
    }

    printk("Bluetooth initialized\n");

    hog_init();

    if (IS_ENABLED(CONFIG_SETTINGS)) {
        settings_load();
    }

    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd,
                          ARRAY_SIZE(sd));
    if (err) {
        printk("Advertising failed to start (err %d)\n", err);
        return;
    }

    printk("Advertising successfully started\n");
}

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey) {
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    printk("Passkey for %s: %06u\n", addr, passkey);
}

static void auth_cancel(struct bt_conn *conn) {
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    printk("Pairing cancelled: %s\n", addr);
}

static struct bt_conn_auth_cb auth_cb_display = {
    .passkey_display = auth_passkey_display,
    .passkey_entry = NULL,
    .cancel = auth_cancel,
};

int main(void) {

    const struct device *mux1, *mux2, *mux3, *kscan;

    printk("Zephyr Example Application %s\n", APP_VERSION_STRING);

    mux1 = DEVICE_DT_GET(DT_NODELABEL(mux1));
    if (!device_is_ready(mux1)) {
        LOG_ERR("Mux not ready");
        return 0;
    }

    mux2 = DEVICE_DT_GET(DT_NODELABEL(mux2));
    if (!device_is_ready(mux2)) {
        LOG_ERR("Mux not ready");
        return 0;
    }

    mux3 = DEVICE_DT_GET(DT_NODELABEL(mux3));
    if (!device_is_ready(mux3)) {
        LOG_ERR("Mux not ready");
        return 0;
    }

    kscan = DEVICE_DT_GET(DT_NODELABEL(kscan));
    if (!device_is_ready(kscan)) {
        LOG_ERR("Kscan not ready");
        return 0;
    }
    printk("Kscan is ready!\n");

    int ret = usb_connect_init();
    if (ret) {
        LOG_ERR("USBConnect error: %d", ret);
        return 0;
    }
    printk("USBConnect is ready!\n");

    // while (true) {
    // if (usb_connect_is_ready()) {
    //     usb_connect_handle_wakeup();
    //     usb_report[2] = 0;
    //     usb_connect_send(usb_report);
    //     k_timeout_t o = {.ticks = 50000};
    //     k_sleep(o);
    //     usb_report[2] = (uint8_t)4;
    //     usb_connect_send(usb_report);
    // }
    // }

    ret = bt_enable(bt_ready);
    if (ret) {
        printk("Bluetooth init failed (err %d)\n", ret);
        return 0;
    }

    if (IS_ENABLED(CONFIG_SAMPLE_BT_USE_AUTHENTICATION)) {
        bt_conn_auth_cb_register(&auth_cb_display);
        printk("Bluetooth authentication callbacks registered.\n");
    }

    hog_button_loop();

    return 0;
}
