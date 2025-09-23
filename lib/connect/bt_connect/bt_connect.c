#include <lib/connect/bt_connect.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>

#include <zephyr/logging/log.h>

#include <zephyr/usb/class/hid.h>

#include <zephyr/settings/settings.h>

#include <stdatomic.h>

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
    .version = 0x0111,
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
    BT_DATA_BYTES(BT_DATA_GAP_APPEARANCE, 0xC1, 0x03),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL),
                  BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)),
};

// BATTERY
static uint8_t batt = 100;
static ssize_t read_batt(struct bt_conn *c, const struct bt_gatt_attr *a,
                         void *buf, uint16_t len, uint16_t off) {
    return bt_gatt_attr_read(c, a, buf, len, off, &batt, sizeof(batt));
}
BT_GATT_SERVICE_DEFINE(
    bas_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_BAS),
    BT_GATT_CHARACTERISTIC(BT_UUID_BAS_BATTERY_LEVEL,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ, read_batt, NULL, &batt),
    BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));
// BATTERY

#define SAMPLE_BT_PERM_READ BT_GATT_PERM_READ_ENCRYPT
#define SAMPLE_BT_PERM_WRITE BT_GATT_PERM_WRITE_ENCRYPT

#if CONFIG_YKB_LEFT
#define CONFIG_BT_DEVICE_NAME_FULL CONFIG_BT_DEVICE_NAME " (Left)"
#elif CONFIG_YKB_RIGHT
#define CONFIG_BT_DEVICE_NAME_FULL CONFIG_BT_DEVICE_NAME " (Right)"
#endif // CONFIG_YKB_LEFT || CONFIG_YKB_RIGHT

static atomic_bool kb_ready = false;

static const uint8_t report_map[] = HID_KEYBOARD_REPORT_DESC();

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

    LOG_INF("Connected to %s", addr);

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

    bt_set_name(CONFIG_BT_DEVICE_NAME_FULL);
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

static void pairing_failed_cb(struct bt_conn *conn,
                              enum bt_security_err reason) {
    if (reason == BT_SECURITY_ERR_PIN_OR_KEY_MISSING) {
        bt_unpair(BT_ID_DEFAULT, bt_conn_get_dst(conn));
        LOG_WRN("Pairing failed (key missing). Cleared local bond to retry.");
    }
}

static void security_changed_cb(struct bt_conn *conn, bt_security_t security,
                                enum bt_security_err error) {
    LOG_INF("BT SEC ERR %d", error);
}

static struct bt_conn_cb conn_cbs = {
    .security_changed = security_changed_cb,
};

static struct bt_conn_auth_info_cb auth_info_cbs = {
    .pairing_failed = pairing_failed_cb,
};

int bt_connect_init() {

    int ret = bt_enable(bt_ready);
    if (ret) {
        LOG_ERR("Bluetooth init failed (err %d)", ret);
        return -1;
    }

    ret = bt_conn_auth_info_cb_register(&auth_info_cbs);
    if (ret) {
        LOG_ERR("Unable to register bt_conn_auth_info_cbs (err %d)", ret);
        return -2;
    }

    ret = bt_conn_cb_register(&conn_cbs);
    if (ret) {
        LOG_ERR("Unable to register bt_conn_cbs (err %d)", ret);
        return -3;
    }

    return 0;
}

static const struct bt_le_adv_param adv_param[] = BT_LE_ADV_PARAM(
    BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_USE_IDENTITY |
        BT_LE_ADV_OPT_USE_TX_POWER, // use the identity address (not RPA)
    BT_GAP_ADV_FAST_INT_MIN_1, BT_GAP_ADV_FAST_INT_MAX_1,
    NULL // default identity
);

void bt_connect_start_advertising(void) {
    int err;

    bt_le_adv_stop();

    err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        LOG_ERR("Adv start failed: %d", err);
        return;
    }

    bt_addr_le_t id_addr;
    bt_id_get(&id_addr, NULL);
    char s[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(&id_addr, s, sizeof(s));
    LOG_INF("Advertising as %s", s);

    LOG_INF("Advertising started");
}

void bt_connect_factory_reset() {
    // TODO: probably not working
    int err;

    err = bt_unpair(BT_ID_DEFAULT, NULL);
    if (err) {
        LOG_ERR("unpair failed: %d", err);
        return;
    }

    settings_delete("bt");
    settings_save();

    LOG_INF("Factory reset done");
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
    bt_gatt_notify(NULL, &hog_svc.attrs[6], buffer,
                   BT_CONNECT_HID_REPORT_COUNT);
}

bool bt_connect_is_ready() {
    return kb_ready;
}
