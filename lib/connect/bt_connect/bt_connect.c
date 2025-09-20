#include <lib/connect/bt_connect.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>

#include <zephyr/usb/class/hid.h>

#include <zephyr/settings/settings.h>

#include "zephyr/logging/log.h"

LOG_MODULE_REGISTER(bt_connect, CONFIG_BT_CONNECT_LOG_LEVEL);

struct hids_info {
    uint16_t version; /* version number of base USB HID Specification */
    uint8_t code;     /* country HID Device hardware is localized for. */
    uint8_t flags;
} __packed;

struct hids_report {
    uint8_t id;   /* report id */
    uint8_t type; /* report type */
} __packed;

static struct hids_info info = {
    .version = 0x0000,
    .code = 0x00,
    .flags = BIT(1)    // NORMALLY_CONNECTABLE
             | BIT(0), // REMOTE_WAKE
};

static struct hids_report input = {
    .id = 0x01,
    .type = 0x01, // HIDS_INPUT
};

static uint8_t simulate_input;
static uint8_t ctrl_point;
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL),
                  BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)),
};

#define SAMPLE_BT_PERM_READ BT_GATT_PERM_READ_ENCRYPT
#define SAMPLE_BT_PERM_WRITE BT_GATT_PERM_WRITE_ENCRYPT

#if CONFIG_YKB_LEFT
#define CONFIG_BT_DEVICE_NAME_FULL CONFIG_BT_DEVICE_NAME " (Left)"
#elif CONFIG_YKB_RIGHT
#define CONFIG_BT_DEVICE_NAME_FULL CONFIG_BT_DEVICE_NAME " (Right)"
#endif // CONFIG_YKB_LEFT || CONFIG_YKB_RIGHT

static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME_FULL,
            sizeof(CONFIG_BT_DEVICE_NAME_FULL) - 1),
};

static bool kb_ready = false;

static const uint8_t report_map[] = HID_KEYBOARD_REPORT_DESC();
// static uint8_t report_map[] = {
//     0x05, 0x01, /* Usage Page (Generic Desktop Ctrls) */
//     0x09, 0x02, /* Usage (Mouse) */
//     0xA1, 0x01, /* Collection (Application) */
//     0x85, 0x01, /*	 Report Id (1) */
//     0x09, 0x01, /*   Usage (Pointer) */
//     0xA1, 0x00, /*   Collection (Physical) */
//     0x05, 0x09, /*     Usage Page (Button) */
//     0x19, 0x01, /*     Usage Minimum (0x01) */
//     0x29, 0x03, /*     Usage Maximum (0x03) */
//     0x15, 0x00, /*     Logical Minimum (0) */
//     0x25, 0x01, /*     Logical Maximum (1) */
//     0x95, 0x03, /*     Report Count (3) */
//     0x75, 0x01, /*     Report Size (1) */
//     0x81, 0x02, /*     Input (Data,Var,Abs,No Wrap,Linear,...) */
//     0x95, 0x01, /*     Report Count (1) */
//     0x75, 0x05, /*     Report Size (5) */
//     0x81, 0x03, /*     Input (Const,Var,Abs,No Wrap,Linear,...) */
//     0x05, 0x01, /*     Usage Page (Generic Desktop Ctrls) */
//     0x09, 0x30, /*     Usage (X) */
//     0x09, 0x31, /*     Usage (Y) */
//     0x15, 0x81, /*     Logical Minimum (129) */
//     0x25, 0x7F, /*     Logical Maximum (127) */
//     0x75, 0x08, /*     Report Size (8) */
//     0x95, 0x02, /*     Report Count (2) */
//     0x81, 0x06, /*     Input (Data,Var,Rel,No Wrap,Linear,...) */
//     0xC0,       /*   End Collection */
//     0xC0,       /* End Collection */
// };
//
// static const uint8_t report_map[] = {
//     0x05, 0x01, /* Usage Page (Generic Desktop) */
//     0x09, 0x06, /* Usage (Keyboard) */
//     0xA1, 0x01, /* Collection (Application)      */
//     0x85, 0x01, /*   Report ID (1)               */
//
//     /* ---- Modifiers (8 bits) ---- */
//     0x05, 0x07, /*   Usage Page (Keyboard/Keypad)        */
//     0x19, 0xE0, /*   Usage Minimum (LeftControl)          */
//     0x29, 0xE7, /*   Usage Maximum (Right GUI)            */
//     0x15, 0x00, /*   Logical Minimum (0)                  */
//     0x25, 0x01, /*   Logical Maximum (1)                  */
//     0x75, 0x01, /*   Report Size (1)                      */
//     0x95, 0x08, /*   Report Count (8)                     */
//     0x81, 0x02, /*   Input (Data,Var,Abs)                 */
//
//     /* ---- Reserved byte (constant) ---- */
//     0x75, 0x08, /*   Report Size (8)                      */
//     0x95, 0x01, /*   Report Count (1)                     */
//     0x81, 0x03, /*   Input (Const,Var,Abs)                */
//
//     /* ---- Keycode array (6 bytes) ---- */
//     0x05, 0x07, /*   Usage Page (Keyboard/Keypad)         */
//     0x19, 0x00, /*   Usage Minimum (0)                    */
//     0x29, 0xE7, /*   Usage Maximum (0xE7)                 */
//     0x15, 0x00, /*   Logical Minimum (0)                  */
//     0x25, 0xE7, /*   Logical Maximum (0xE7)               */
//     0x75, 0x08, /*   Report Size (8)                      */
//     0x95, 0x06, /*   Report Count (6)                     */
//     0x81, 0x00, /*   Input (Data,Array,Abs)               */
//
//     /* ---- LED output report (5 bits) ---- */
//     0x05, 0x08, /*   Usage Page (LEDs)                    */
//     0x19, 0x01, /*   Usage Minimum (Num Lock)             */
//     0x29, 0x05, /*   Usage Maximum (Kana)                 */
//     0x75, 0x01, /*   Report Size (1)                      */
//     0x95, 0x05, /*   Report Count (5)                     */
//     0x91, 0x02, /*   Output (Data,Var,Abs)                */
//
//     /* ---- LED padding (3 bits) ---- */
//     0x75, 0x03, /*   Report Size (3)                      */
//     0x95, 0x01, /*   Report Count (1)                     */
//     0x91, 0x03, /*   Output (Const,Var,Abs)               */
//
//     0xC0 /* End Collection                         */
// };

static ssize_t read_info(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                         void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
                             sizeof(struct hids_info));
}

static ssize_t read_report_map(struct bt_conn *conn,
                               const struct bt_gatt_attr *attr, void *buf,
                               uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, report_map,
                             sizeof(report_map));
}

static ssize_t read_report(struct bt_conn *conn,
                           const struct bt_gatt_attr *attr, void *buf,
                           uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
                             sizeof(struct hids_report));
}

static void input_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    simulate_input = (value == BT_GATT_CCC_NOTIFY) ? 1 : 0;
}

static ssize_t read_input_report(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr, void *buf,
                                 uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, NULL, 0);
}

static ssize_t write_ctrl_point(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len, uint16_t offset,
                                uint8_t flags) {
    uint8_t *value = attr->user_data;

    if (offset + len > sizeof(ctrl_point)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    memcpy(value + offset, buf, len);

    return len;
}

static void connected(struct bt_conn *conn, uint8_t err) {
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (err) {
        LOG_ERR("Failed to connect to %s, err 0x%02x %s", addr, err,
                bt_hci_err_to_str(err));
        return;
    }

    kb_ready = true;
    LOG_INF("Connected %s", addr);

    if (bt_conn_set_security(conn, BT_SECURITY_L2)) {
        LOG_ERR("Failed to set security");
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason) {
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    kb_ready = false;

    LOG_ERR("Disconnected from %s, reason 0x%02x %s", addr, reason,
            bt_hci_err_to_str(reason));
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
                             enum bt_security_err err) {
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (!err) {
        LOG_INF("Security changed: %s level %u", addr, level);
    } else {
        LOG_ERR("Security failed: %s level %u err %s(%d)", addr, level,
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
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return;
    }

    LOG_INF("Bluetooth initialized");

    if (IS_ENABLED(CONFIG_SETTINGS)) {
        settings_load();
    }

    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd,
                          ARRAY_SIZE(sd));

    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return;
    }

    LOG_INF("Advertising successfully started");
}

#if CONFIG_BT_INTER_KB_COMM
#if (CONFIG_BT_CONNECT_MASTER_RIGHT && CONFIG_YKB_RIGHT) ||                    \
    (CONFIG_BT_CONNECT_MASTER_LEFT && CONFIG_YKB_LEFT)
#define IS_INTER_COMM_MASTER 1
#else
#define IS_INTER_COMM_MASTER 0
#endif // CONFIG_BT_CONNECT_MASTER_RIGHT && CONFIG_YKB_RIGHT
#else
#define IS_INTER_COMM_MASTER 0
#endif // CONFIG_BT_INTER_KB_COMM

int bt_connect_init() {

    int ret = bt_enable(bt_ready);
    if (ret) {
        LOG_ERR("Bluetooth init failed (err %d)", ret);
        return -1;
    }

    return 0;
}

BT_GATT_SERVICE_DEFINE(
    hog_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO, BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ, read_info, NULL, &info),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP, BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ, read_report_map, NULL, NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           SAMPLE_BT_PERM_READ, read_input_report, NULL, NULL),
    BT_GATT_CCC(input_ccc_changed, SAMPLE_BT_PERM_READ | SAMPLE_BT_PERM_WRITE),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ, read_report,
                       NULL, &input),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT,
                           BT_GATT_CHRC_WRITE_WITHOUT_RESP, BT_GATT_PERM_WRITE,
                           NULL, write_ctrl_point, &ctrl_point), );

void bt_connect_send(uint8_t buffer[BT_CONNECT_HID_REPORT_COUNT]) {
    bt_gatt_notify(NULL, &hog_svc.attrs[5], buffer,
                   BT_CONNECT_HID_REPORT_COUNT);
}

bool bt_connect_is_ready() {
    return kb_ready;
}
