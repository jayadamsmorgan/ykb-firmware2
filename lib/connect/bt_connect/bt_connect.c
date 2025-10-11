
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/uuid.h>
#if CONFIG_BT_INTER_KB_COMM
#include "inter_kb_comm/inter_kb_comm.h"

#if CONFIG_BT_INTER_KB_COMM_MASTER
#include "inter_kb_comm/master.h"
#endif
#include <lib/connect/bt_connect.h>
#if CONFIG_BT_INTER_KB_COMM_SLAVE
#include "inter_kb_comm/slave.h"
#endif
#endif // CONFIG_BT_INTER_KB_COMM

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/usb/class/hid.h>

#include <stdatomic.h>
#include <string.h>

LOG_MODULE_REGISTER(bt_connect, CONFIG_BT_CONNECT_LOG_LEVEL);

struct hids_info {
    uint16_t version;
    uint8_t code;
    uint8_t flags;
} __packed;

struct hids_report {
    uint8_t id;
    uint8_t type;
} __packed;

static struct hids_info info = {
    .version = 0x0111,
    .code = 0x00,
    .flags = BIT(1)    /* NORMALLY_CONNECTABLE */
             | BIT(0), /* REMOTE_WAKE */
};

static struct hids_report input = {
    .id = 0x01, .type = 0x01, /* HIDS_INPUT */
};

/* Advertising
 *
 * - Master build: advertise HID/BAS and the Split Service UUID (double
 * peripheral).
 * - Slave build:    (slave is central) it does NOT advertise the split service
 * here.
 */
#if CONFIG_BT_INTER_KB_COMM_MASTER
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_GAP_APPEARANCE, 0xC1, 0x03),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL),
                  BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)),
    /* Master exposes the split service as a peripheral for the slave (central).
     */
    BT_DATA(BT_DATA_UUID128_ALL, ykb_svc_uuid_le, sizeof(ykb_svc_uuid_le)),
};

static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
            sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};
#endif

static const uint8_t report_map[] = HID_KEYBOARD_REPORT_DESC();

static bool bt_kb_ready;
static uint8_t ctrl_point;

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
    bt_kb_ready = (value == BT_GATT_CCC_NOTIFY);
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

static void bt_ready(int err) {
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return;
    }

    LOG_INF("Bluetooth initialized");

    err = settings_load();
    if (err) {
        LOG_ERR("Unable to load settings (err %d)", err);
        return;
    }

#if CONFIG_BT_INTER_KB_COMM_MASTER
    // err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd,
    //                       ARRAY_SIZE(sd));
    // LOG_INF("adv start rc=%d", err);
    ykb_master_link_start();

#endif // CONFIG_BT_INTER_KB_COMM_SLAVE

#if CONFIG_BT_INTER_KB_COMM_SLAVE
    /* Slave is central → start scanning/connecting to master’s split
       service */
    ykb_slave_link_start();
#endif

    LOG_INF("Advertising successfully started");
}

int bt_connect_init(void) {
    int ret = bt_enable(bt_ready);
    if (ret) {
        LOG_ERR("Bluetooth enable failed (err %d)", ret);
        return -1;
    }
    return 0;
}

void bt_connect_factory_reset(void) {
    bt_unpair(BT_ID_DEFAULT, NULL);
    settings_delete("bt");
    settings_save();
}

void bt_connect_start_advertising(void) {
#if CONFIG_BT_INTER_KB_COMM_MASTER
    int err = bt_le_adv_stop();
    if (err) {
        LOG_WRN("Unable to stop advertising (err %d)", err);
    }

    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd,
                          ARRAY_SIZE(sd));
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
    }
#endif // CONFIG_BT_INTER_KB_COMM_MASTER
}

/* HID service to the host (unchanged layout) */
#if CONFIG_BT_INTER_KB_COMM_MASTER

BT_GATT_SERVICE_DEFINE(
    hog_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO, BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ, read_info, NULL, &info),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP, BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ, read_report_map, NULL, NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT, read_input_report, NULL,
                           NULL),
    BT_GATT_CCC(input_ccc_changed,
                BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ, read_report,
                       NULL, &input),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT,
                           BT_GATT_CHRC_WRITE_WITHOUT_RESP, BT_GATT_PERM_WRITE,
                           NULL, write_ctrl_point, &ctrl_point));

#endif // CONFIG_BT_INTER_KB_COMM_MASTER
/* Send HID input up to the host.
 *
 * MASTER build:
 *   - Merge in the last 8-byte report received from the slave (via KEYS_RX
 * WNR). SLAVE build:
 *   - If connected to master, push to master (WNR path in slave.c).
 *   - Else (fallback), notify locally (e.g., standalone mode if you also expose
 * HID here).
 */
void bt_connect_send(uint8_t report[BT_CONNECT_HID_REPORT_COUNT],
                     uint8_t report_size) {
#if CONFIG_BT_INTER_KB_COMM_MASTER
    ykb_master_merge_reports(report, report_size);
    bt_gatt_notify(NULL, &hog_svc.attrs[6], report,
                   BT_CONNECT_HID_REPORT_COUNT);

#elif CONFIG_BT_INTER_KB_COMM_SLAVE
    if (ykb_slave_is_connected()) {
        ykb_slave_send_keys(report); /* write WNR upstream to master */
    } else {
        /* optional standalone behavior */
        // bt_gatt_notify(NULL, &hog_svc.attrs[6], report,
        //                BT_CONNECT_HID_REPORT_COUNT);
    }

#else
    bt_gatt_notify(NULL, &hog_svc.attrs[6], report,
                   BT_CONNECT_HID_REPORT_COUNT);
#endif
}

bool bt_connect_is_ready(void) {
#if CONFIG_BT_INTER_KB_COMM_SLAVE
    return ykb_slave_is_connected();
#else
    return bt_kb_ready;
#endif
}

/* BAS helpers (unchanged) */
void bt_connect_set_battery_charging(void) {
    bt_bas_bls_set_battery_charge_state(BT_BAS_BLS_CHARGE_STATE_CHARGING);
}

void bt_connect_set_battery_discharging(void) {
    bt_bas_bls_set_battery_charge_state(
        BT_BAS_BLS_CHARGE_STATE_DISCHARGING_INACTIVE);
}

void bt_connect_set_battery_disconnected(void) {
    bt_bas_bls_set_battery_present(BT_BAS_BLS_BATTERY_NOT_PRESENT);
}

void bt_connect_set_battery_connected(void) {
    bt_bas_bls_set_battery_present(BT_BAS_BLS_BATTERY_PRESENT);
}

void bt_connect_set_battery_level(uint8_t level) {
    if (level > 100)
        level = 100;
    int res = bt_bas_set_battery_level(level);
    if (res) {
        LOG_ERR("Unable to set battery level (err %d)", res);
    }
}
