#include "slave.h"

#include "inter_kb_comm.h"
#include "inter_kb_proto.h"

#include <lib/keyboard/kb_settings.h>

#include <lib/led/kb_backlight.h>
#include <lib/led/kb_backlight_settings.h>
#include <lib/led/kb_backlight_state.h>

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

void handle_incoming_packet(struct inter_kb_proto *packet, uint16_t data_len) {
    switch (packet->data_type) {
    case INTER_KB_PROTO_DATA_TYPE_KB_SETTINGS: {
        struct kb_settings_image img;
        memcpy(&img, packet->data, data_len);
        if (img.version != KB_SETTINGS_IMAGE_VERSION) {
            LOG_ERR("Unsupported KBSettings image");
        }
        kb_settings_load_from_image(&img);
        kb_settings_save_from_image(&img);
        LOG_INF("New KBSettings applied.");
        break;
    }
    case INTER_KB_PROTO_DATA_TYPE_BL_STATE: {
        backlight_state_img img;
        memcpy(&img, packet->data, data_len);
        if (img.version != KB_BL_SETTINGS_IMAGE_VERSION) {
            LOG_ERR("Unsupported KBBLSettings image");
        }
        kb_bl_settings_load_from_image(&img);
        kb_backlight_set_mode(img.mode_idx);
        // No need to call bl settings save here,
        // it should've saved them in kb_backlight_set_mode
        LOG_INF("New KBSettings applied.");
        break;
    }
    default: {
        LOG_ERR("Unknown data type %d", packet->data_type);
        break;
    }
    }
}

ssize_t write_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                 const void *buf, uint16_t len, uint16_t offset,
                 uint8_t flags) {

    struct inter_kb_proto data;
    int res = inter_kb_proto_parse((uint8_t *)buf, len, &data);
    if (res <= 0) {
        LOG_ERR("Unable parse incoming packet: %d", res);
        return len;
    }

    handle_incoming_packet(&data, res);

    return len;
}

BT_GATT_SERVICE_DEFINE(
    ykb_split_svc, BT_GATT_PRIMARY_SERVICE(&YKB_SPLIT_SVC_UUID),
    BT_GATT_CHARACTERISTIC(&YKB_STATE_CHRC_UUID.uuid,
                           BT_GATT_CHRC_WRITE_WITHOUT_RESP, BT_GATT_PERM_WRITE,
                           NULL, write_cb, NULL),
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
    return ykb_master_conn && ykb_ccc_enabled;
}

static const struct bt_gatt_attr *ykb_val = &ykb_split_svc.attrs[3]; // value

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
