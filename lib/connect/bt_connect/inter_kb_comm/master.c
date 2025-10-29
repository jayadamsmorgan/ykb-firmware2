#include "master.h"

#include "inter_kb_comm.h"
#include "inter_kb_proto.h"

#include <lib/led/kb_backlight_settings.h>

#include <lib/keyboard/kb_handle.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/uuid.h>

#include <zephyr/logging/log.h>

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_GAP_APPEARANCE, 0xC1, 0x03),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL),
                  BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)),
#if CONFIG_BT_INTER_KB_COMM_SLAVE
    BT_DATA(BT_DATA_UUID128_ALL, ykb_svc_uuid_le,
            sizeof(ykb_svc_uuid_le)), // <- сюда
#endif
};

static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
            sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};
LOG_MODULE_DECLARE(bt_connect, CONFIG_BT_CONNECT_LOG_LEVEL);

static struct bt_conn *ykb_slave_conn;

static bool uuid_match_cb(struct bt_data *data, void *user_data) {
    if (data->type == BT_DATA_UUID128_ALL ||
        data->type == BT_DATA_UUID128_SOME) {
        if (data->data_len % 16 == 0) {
            for (int i = 0; i < data->data_len; i += 16) {
                if (!memcmp(&data->data[i], ykb_svc_uuid_le, 16)) {
                    *(bool *)user_data = true;
                    return false; // stop parsing on match
                }
            }
        }
    }
    return true; // continue parsing
}

static bool adv_has_split_uuid(struct net_buf_simple *ad) {
    bool found = false;
    bt_data_parse(ad, uuid_match_cb, &found);
    return found;
}

static struct bt_gatt_discover_params disc_params;
static struct bt_gatt_subscribe_params sub_params;

// TODO: mutex or sem
static uint32_t incoming_keys[KB_BITMAP_WORDS_SLAVE];

static uint16_t ykb_start_handle, ykb_end_handle;
static uint16_t ykb_value_handle, ykb_ccc_handle;

static uint8_t ykb_notify_cb(struct bt_conn *conn,
                             struct bt_gatt_subscribe_params *params,
                             const void *data, uint16_t len) {

    if (!data)
        return BT_GATT_ITER_STOP;

    if (len > sizeof(struct inter_kb_proto)) {
        LOG_ERR("Incoming BLE packet too big");
        return BT_GATT_ITER_CONTINUE;
    }

    // May be a good idea to create k_work here instead ?

    struct inter_kb_proto packet = {0};
    int res = inter_kb_proto_parse((uint8_t *)data, len, &packet);
    if (res <= 0) {
        LOG_ERR("Unable to parse IKBP packet (err %d)", res);
        return BT_GATT_ITER_CONTINUE;
    }

    if (packet.data_type == INTER_KB_PROTO_DATA_TYPE_KEYS) {
        // TODO: mutex or sem
        memcpy(incoming_keys, packet.data, MIN(res, sizeof(incoming_keys)));
    } else {
        LOG_WRN("Unsupported IKBP packet data type %d", packet.data_type);
        return BT_GATT_ITER_CONTINUE;
    }

    return BT_GATT_ITER_CONTINUE;
}

static uint8_t ykb_discover_func(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr,
                                 struct bt_gatt_discover_params *params) {
    if (!attr) {
        LOG_WRN("Discovery finished with no match (type=%u)", params->type);
        return BT_GATT_ITER_STOP;
    }

    switch (params->type) {
    case BT_GATT_DISCOVER_PRIMARY: {
        const struct bt_gatt_service_val *prim = attr->user_data;
        ykb_start_handle = attr->handle + 1;
        ykb_end_handle = prim->end_handle;

        disc_params.uuid = &YKB_KEYS_CHRC_UUID.uuid;
        disc_params.start_handle = ykb_start_handle;
        disc_params.end_handle = ykb_end_handle;
        disc_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;
        bt_gatt_discover(conn, &disc_params);
        return BT_GATT_ITER_STOP;
    }
    case BT_GATT_DISCOVER_CHARACTERISTIC: {
        const struct bt_gatt_chrc *chrc = attr->user_data;
        ykb_value_handle = chrc->value_handle;

        disc_params.uuid = BT_UUID_GATT_CCC;
        disc_params.start_handle = ykb_value_handle + 1;
        disc_params.end_handle = ykb_end_handle;
        disc_params.type = BT_GATT_DISCOVER_DESCRIPTOR;
        bt_gatt_discover(conn, &disc_params);
        return BT_GATT_ITER_STOP;
    }
    case BT_GATT_DISCOVER_DESCRIPTOR: {
        ykb_ccc_handle = attr->handle;

        memset(&sub_params, 0, sizeof(sub_params));
        sub_params.ccc_handle = ykb_ccc_handle;
        sub_params.value_handle = ykb_value_handle;
        sub_params.value = BT_GATT_CCC_NOTIFY;
        sub_params.notify = ykb_notify_cb;

        int rc = bt_gatt_subscribe(conn, &sub_params);
        LOG_INF("bt_gatt_subscribe rc=%d (val=%u, ccc=%u)", rc,
                ykb_value_handle, ykb_ccc_handle);
        return BT_GATT_ITER_STOP;
    }
    default:
        return BT_GATT_ITER_STOP;
    }
}

static void ykb_start_discovery(struct bt_conn *conn) {
    disc_params.uuid = &YKB_SPLIT_SVC_UUID.uuid;
    disc_params.func = ykb_discover_func;
    disc_params.start_handle = 0x0001;
    disc_params.end_handle = 0xFFFF;
    disc_params.type = BT_GATT_DISCOVER_PRIMARY;
    bt_gatt_discover(conn, &disc_params);
}

static void ykb_device_found(const bt_addr_le_t *addr, int8_t rssi,
                             uint8_t adv_type, struct net_buf_simple *ad) {

    if (!adv_has_split_uuid(ad))
        return;

    LOG_INF("It is left keyboard!!!");

    if (ykb_slave_conn)
        return;

    LOG_INF("First time keyboard registration");

    struct bt_le_conn_param conn_param = {
        .interval_min = 6,
        .interval_max = 8,
        .latency = 0,
        .timeout = 400,
    };

    bt_le_scan_stop();
    bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, &conn_param,
                      &ykb_slave_conn);
}

static void ykb_master_connected(struct bt_conn *conn, uint8_t err) {

    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    struct bt_conn_info conn_info;

    int res = bt_conn_get_info(conn, &conn_info);
    if (res) {
        LOG_ERR("Unable to get connection info (err %d)", res);
        return;
    }

    if (conn_info.role == BT_CONN_ROLE_CENTRAL) {
        const struct bt_conn_le_info *le = &conn_info.le;
        /* interval units are 1.25ms; timeout units are 10ms */
        LOG_INF("CENTRAL link params: interval=%u*1.25ms (~%u ms) latency=%u "
                "timeout=%u*10ms",
                le->interval, le->interval * 125 / 100, le->latency,
                le->timeout);
    } else {
        const struct bt_conn_le_info *le = &conn_info.le;
        LOG_INF("PERIPHERAL link params: interval=%u*1.25ms (~%u ms) "
                "latency=%u timeout=%u*10ms",
                le->interval, le->interval * 125 / 100, le->latency,
                le->timeout);
    }

    if (conn_info.role != BT_CONN_ROLE_CENTRAL) {

        if (err) {
            LOG_ERR("Failed to connect to host %s, err 0x%02x %s", addr, err,
                    bt_hci_err_to_str(err));
            return;
        }

        LOG_INF("Connected to host %s", addr);

        if (bt_conn_set_security(conn, BT_SECURITY_L2)) {
            LOG_ERR("Failed to set security");
        }

        return;
    }

    if (err) {
        LOG_ERR("Peer connect failed: 0x%02x", err);
        ykb_slave_conn = NULL;
        bt_le_scan_start(BT_LE_SCAN_ACTIVE, ykb_device_found);
        return;
    }
    static const struct bt_le_conn_param fast = {
        .interval_min = 6,
        .interval_max = 8,
        .latency = 0,
        .timeout = 400,
    };
    bt_conn_le_param_update(conn, &fast);
    LOG_INF("Peer connected");

    ykb_start_discovery(conn);
}
static bool slave_was_disconnected = false;
static void ykb_master_disconnected(struct bt_conn *conn, uint8_t reason) {
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_ERR("Disconnected from %s, reason 0x%02x %s", addr, reason,
            bt_hci_err_to_str(reason));

    if (conn == ykb_slave_conn) {
        bt_conn_unref(ykb_slave_conn);

        ykb_slave_conn = NULL;
        slave_was_disconnected = true;
        return;
    }

    // if (reason == BT_HCI_ERR_REMOTE_USER_TERM_CONN) {
    //     int res = bt_unpair(BT_ID_DEFAULT, bt_conn_get_dst(conn));
    //     if (res) {
    //         LOG_ERR("Unable to unpair device %s (err %d)", addr, res);
    //         return;
    //     }
    //     LOG_INF("Unpaired device %s", addr);
    // }
}

static void ykb_master_recycled(void) {
    // Check if slave connection was recylced:
    if (slave_was_disconnected) {
        LOG_WRN("Peer disconnected, rescanning...");
        bt_le_scan_start(BT_LE_SCAN_ACTIVE, ykb_device_found);
        slave_was_disconnected = false;
    } else {
        LOG_WRN("Host disconnected, start advertising...");
        int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd,
                                  ARRAY_SIZE(sd));
    }
}

static void conn_param_updated(struct bt_conn *conn, uint16_t interval,
                               uint16_t latency, uint16_t timeout) {
    printk("Connection parameters updated: interval=%u*1.25ms (~%u ms), "
           "latency=%u, timeout=%u*10ms (~%u ms)\n",
           interval, interval * 125 / 100, latency, timeout, timeout * 10);
}
static void security_changed(struct bt_conn *conn, bt_security_t level,
                             enum bt_security_err err) {
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (!err) {
        struct bt_le_conn_param param = {
            .interval_min = 6,
            .interval_max = 12,
            .latency = 0,
            .timeout = 400,
        };
        int ret = bt_conn_le_param_update(conn, &param);
        if (ret) {
            printk("Failed to request connection parameter update: %d\n", ret);
        } else {
            printk("Connection parameter update requested\n");
        }
        LOG_INF("Security changed: %s level %u", addr, level);
    } else {
        LOG_ERR("Security failed: %s level %u err %s(%d)", addr, level,
                bt_security_err_to_str(err), err);
    }
}
BT_CONN_CB_DEFINE(ykb_peer_cb) = {
    .connected = ykb_master_connected,
    .disconnected = ykb_master_disconnected,
    .security_changed = security_changed,
    .le_param_updated = conn_param_updated,
    .recycled = ykb_master_recycled,
};

void ykb_master_link_start() {
    int err = bt_le_scan_start(BT_LE_SCAN_ACTIVE, ykb_device_found);
    LOG_INF("Scan start with code: %d", err);
}

void ykb_master_link_stop() {
    bt_le_scan_stop();
}

int bt_connect_get_slave_keys(uint32_t *bm, size_t bm_byte_size) {
    if (!ykb_slave_conn) {
        return -1;
    }
    // TODO: sem or mut
    memcpy(bm, incoming_keys, bm_byte_size);
    return 0;
}

void bt_connect_send_master_kb_settings() {
    struct kb_settings_image img;
    kb_settings_build_image_from_runtime(&img);
    struct inter_kb_proto data;
    int res = inter_kb_proto_new(INTER_KB_PROTO_DATA_TYPE_KB_SETTINGS, &img,
                                 sizeof(img), &data);
    if (res <= 0) {
        LOG_ERR("Unable to create IKBP packet: %d", res);
        return;
    }

    struct bt_gatt_write_params params = {
        .data = &data,
        .handle = 2,
        .length = res,
    };
    bt_gatt_write(ykb_slave_conn, &params);
}

void bt_connect_send_master_bl_state() {
    backlight_state_img img;
    kb_backlight_settings_build_image_from_runtime(&img);
    struct inter_kb_proto data;
    int res = inter_kb_proto_new(INTER_KB_PROTO_DATA_TYPE_BL_STATE, &img,
                                 sizeof(img), &data);
    if (res <= 0) {
        LOG_ERR("Unable to create IKBP packet: %d", res);
        return;
    }

    struct bt_gatt_write_params params = {
        .data = &data,
        .handle = 2,
        .length = res,
    };
    bt_gatt_write(ykb_slave_conn, &params);
}
