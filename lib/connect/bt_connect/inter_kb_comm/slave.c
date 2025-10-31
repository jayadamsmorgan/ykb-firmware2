#include "slave.h"

#include "inter_kb_comm.h"
#include "inter_kb_proto.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/uuid.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(bt_connect, CONFIG_BT_CONNECT_LOG_LEVEL);

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_GAP_APPEARANCE, 0xC1, 0x03),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL),
                  BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)),
    BT_DATA(BT_DATA_UUID128_ALL, ykb_svc_uuid_le, sizeof(ykb_svc_uuid_le)),
};

static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
            sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

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
static void ykb_peer_recycled(void) {
    LOG_INF("Start new advertisong procedure");
    bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd,
                    ARRAY_SIZE(sd));
}
BT_CONN_CB_DEFINE(peer_cb) = {
    .connected = ykb_peer_connected,
    .disconnected = ykb_peer_disconnected,
    .recycled = ykb_peer_recycled,
};

bool ykb_slave_is_connected() {
    return ykb_master_conn && ykb_ccc_enabled;
}

static const struct bt_gatt_attr *ykb_val = &ykb_split_svc.attrs[2]; // value

void bt_connect_send_slave_keys(uint32_t *bm, size_t bm_len) {
    if (!ykb_master_conn) {
        return;
    }
    if (!bt_gatt_is_subscribed(ykb_master_conn, ykb_val, BT_GATT_CCC_NOTIFY)) {
        return;
    }
    struct inter_kb_proto data;
    int res =
        inter_kb_proto_new(INTER_KB_PROTO_DATA_TYPE_KEYS, bm, bm_len, &data);
    if (res <= 0) {
        LOG_ERR("Unable to pack IKBP (err %d)", res);
        return;
    }

    int rc = bt_gatt_notify(ykb_master_conn, ykb_val, &data, res);
    if (rc) {
        LOG_ERR("bt_gatt_notify rc=%d", rc);
    }
}
