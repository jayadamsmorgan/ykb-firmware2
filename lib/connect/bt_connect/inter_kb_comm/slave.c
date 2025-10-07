#include "slave.h"

#include "inter_kb_comm.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/uuid.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(bt_connect, CONFIG_BT_CONNECT_LOG_LEVEL);

static uint8_t ykb_ccc_enabled;
static struct bt_conn *ykb_master_conn;

static void ykb_ccc_cfg_changed(const struct bt_gatt_attr *attr,
                                uint16_t value) {
    LOG_INF("ykb_ccc_cfg_changed");
    ykb_ccc_enabled = (value == BT_GATT_CCC_NOTIFY);
}

BT_GATT_SERVICE_DEFINE(
    ykb_split_svc, BT_GATT_PRIMARY_SERVICE(&YKB_SPLIT_SVC_UUID),
    BT_GATT_CHARACTERISTIC(&YKB_KEYS_CHRC_UUID.uuid, BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE, NULL, NULL, NULL),
    BT_GATT_CCC(ykb_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

static void ykb_peer_connected(struct bt_conn *conn, uint8_t err) {

    LOG_INF("We are connected!");
    if (!err)
        ykb_master_conn = bt_conn_ref(conn);
}

static void ykb_peer_disconnected(struct bt_conn *conn, uint8_t reason) {
    if (ykb_master_conn == conn) {
        LOG_INF("We are disconnected!");
        bt_conn_unref(ykb_master_conn);
        ykb_master_conn = NULL;
    }
}

BT_CONN_CB_DEFINE(peer_cb) = {
    .connected = ykb_peer_connected,
    .disconnected = ykb_peer_disconnected,
};

bool ykb_slave_is_connected() {
    // if (ykb_master_conn)
    //     LOG_INF("ykb_master_conn not NULL");
    // if (ykb_ccc_enabled)
    //     LOG_INF("ykb_ccc_enabled not NULL");
    return ykb_master_conn && ykb_ccc_enabled;
}

static const struct bt_gatt_attr *ykb_val = &ykb_split_svc.attrs[2]; // value

void ykb_slave_send_keys(const uint8_t data[8]) {
    if (!ykb_master_conn) {
        return;
    }
    if (!bt_gatt_is_subscribed(ykb_master_conn, ykb_val, BT_GATT_CCC_NOTIFY)) {
        return;
    }
    int rc = bt_gatt_notify(ykb_master_conn, ykb_val, data, 8);
    if (rc) {
        LOG_ERR("bt_gatt_notify rc=%d", rc);
    }
}
