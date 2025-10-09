#include "slave.h"
#include "inter_kb_comm.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>

#include <string.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(bt_connect, CONFIG_BT_CONNECT_LOG_LEVEL);

static struct bt_conn *master_conn;

static struct bt_gatt_discover_params disc;
static struct bt_gatt_subscribe_params sub;

static uint16_t svc_start, svc_end;
static uint16_t keys_rx_handle;  /* value handle to write WNR to (→ master) */
static uint16_t state_tx_handle; /* value handle to subscribe (← master) */
static uint16_t state_ccc_handle;

static bool ykb_connected;
static bool state_tx_subscribed;

/* --- Discovery & subscription ------------------------------------------------
 */

static uint8_t state_notify_cb(struct bt_conn *c,
                               struct bt_gatt_subscribe_params *p,
                               const void *data, uint16_t len) {
    if (!data) {
        state_tx_subscribed = false;
        LOG_WRN("STATE_TX notifications stopped by peer");
        return BT_GATT_ITER_STOP;
    }

    if (len == 8) {
        /* If you want to apply LED/Caps/etc from master, do it here */
        // ykb_slave_apply_state((const uint8_t *)data);
    }
    return BT_GATT_ITER_CONTINUE;
}

static uint8_t discover_cb(struct bt_conn *c, const struct bt_gatt_attr *attr,
                           struct bt_gatt_discover_params *params) {
    if (!attr) {
        LOG_WRN("Discovery finished (type=%u)", params->type);
        return BT_GATT_ITER_STOP;
    }

    switch (params->type) {
    case BT_GATT_DISCOVER_PRIMARY: {
        const struct bt_gatt_service_val *prim = attr->user_data;
        svc_start = attr->handle + 1;
        svc_end = prim->end_handle;

        disc.uuid = &YKB_KEYS_RX_UUID.uuid;
        disc.start_handle = svc_start;
        disc.end_handle = svc_end;
        disc.type = BT_GATT_DISCOVER_CHARACTERISTIC;
        bt_gatt_discover(c, &disc);
        return BT_GATT_ITER_STOP;
    }

    case BT_GATT_DISCOVER_CHARACTERISTIC: {
        const struct bt_gatt_chrc *chrc = attr->user_data;

        if (bt_uuid_cmp(chrc->uuid, &YKB_KEYS_RX_UUID.uuid) == 0) {
            keys_rx_handle = chrc->value_handle;

            disc.uuid = &YKB_STATE_TX_UUID.uuid;
            disc.start_handle = svc_start;
            disc.end_handle = svc_end;
            disc.type = BT_GATT_DISCOVER_CHARACTERISTIC;
            bt_gatt_discover(c, &disc);
            return BT_GATT_ITER_STOP;

        } else {
            /* We found STATE_TX characteristic */
            state_tx_handle = chrc->value_handle;

            disc.uuid = BT_UUID_GATT_CCC;
            disc.start_handle = state_tx_handle + 1;
            disc.end_handle = svc_end;
            disc.type = BT_GATT_DISCOVER_DESCRIPTOR;
            bt_gatt_discover(c, &disc);
            return BT_GATT_ITER_STOP;
        }
    }

    case BT_GATT_DISCOVER_DESCRIPTOR: {
        state_ccc_handle = attr->handle;

        memset(&sub, 0, sizeof(sub));
        sub.ccc_handle = state_ccc_handle;
        sub.value_handle = state_tx_handle;
        sub.value = BT_GATT_CCC_NOTIFY;
        sub.notify = state_notify_cb;

        int rc = bt_gatt_subscribe(c, &sub);
        state_tx_subscribed = (rc == 0);
        LOG_INF("Subscribe STATE_TX rc=%d (val=%u ccc=%u)", rc, state_tx_handle,
                state_ccc_handle);
        return BT_GATT_ITER_STOP;
    }

    default:
        return BT_GATT_ITER_STOP;
    }
}

static void start_discovery(struct bt_conn *c) {
    memset(&disc, 0, sizeof(disc));
    disc.uuid = &YKB_SPLIT_SVC_UUID.uuid;
    disc.func = discover_cb;
    disc.start_handle = 0x0001;
    disc.end_handle = 0xFFFF;
    disc.type = BT_GATT_DISCOVER_PRIMARY;
    bt_gatt_discover(c, &disc);
}

/* --- Central scan/connect to master -----------------------------------------
 */

static bool uuid_match_cb(struct bt_data *data, void *user_data) {
    if (data->type == BT_DATA_UUID128_ALL ||
        data->type == BT_DATA_UUID128_SOME) {
        if (data->data_len % 16 == 0) {
            for (int i = 0; i < data->data_len; i += 16) {
                if (!memcmp(&data->data[i], ykb_svc_uuid_le, 16)) {
                    *(bool *)user_data = true;
                    return false; /* stop parsing on match */
                }
            }
        }
    }
    return true; /* continue parsing */
}

static bool adv_has_split_uuid(struct net_buf_simple *ad) {
    bool found = false;
    bt_data_parse(ad, uuid_match_cb, &found);
    return found;
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi,
                         uint8_t adv_type, struct net_buf_simple *ad) {
    if (!adv_has_split_uuid(ad)) {
        return;
    }

    if (master_conn) {
        return;
    }

    LOG_INF("Found master advertising split UUID; connecting...");

    const struct bt_le_conn_param conn_param = {
        .interval_min = 6,  /* 7.5 ms */
        .interval_max = 12, /* 15  ms */
        .latency = 0,
        .timeout = 400, /* 4 s */
    };

    bt_le_scan_stop();
    int rc = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, &conn_param,
                               &master_conn);
    LOG_INF("bt_conn_le_create rc=%d", rc);
}

static void peer_connected(struct bt_conn *conn, uint8_t err) {
    if (err) {
        LOG_ERR("Central connect failed: 0x%02x %s", err,
                bt_hci_err_to_str(err));
        master_conn = NULL;
        bt_le_scan_start(BT_LE_SCAN_ACTIVE, device_found);
        return;
    }

    struct bt_conn_info info;
    if (!bt_conn_get_info(conn, &info) && info.role == BT_CONN_ROLE_CENTRAL) {
        LOG_INF("Connected to master as CENTRAL");
        /* Tighten link if needed */
        static const struct bt_le_conn_param fast = {.interval_min = 6,
                                                     .interval_max = 12,
                                                     .latency = 0,
                                                     .timeout = 400};
        bt_conn_le_param_update(conn, &fast);

        /* Optional: DLE/PHY */
        // bt_conn_le_data_len_update(conn, NULL);
        // bt_conn_le_phy_update(conn, BT_CONN_LE_PHY_PARAM_2M);
    }

    ykb_connected = true;
    start_discovery(conn);
}

static void peer_disconnected(struct bt_conn *conn, uint8_t reason) {
    if (conn == master_conn) {
        LOG_WRN("Disconnected from master: 0x%02x %s", reason,
                bt_hci_err_to_str(reason));
        bt_conn_unref(master_conn);
        master_conn = NULL;
        ykb_connected = false;
        state_tx_subscribed = false;
        keys_rx_handle = state_tx_handle = state_ccc_handle = 0;

        /* Resume scanning */
        bt_le_scan_start(BT_LE_SCAN_ACTIVE, device_found);
    }
}

BT_CONN_CB_DEFINE(slave_central_cb) = {
    .connected = peer_connected,
    .disconnected = peer_disconnected,
};

/* Public: start central link logic from bt_ready() in bt_connect.c */
void ykb_slave_link_start(void) {
    int err = bt_le_scan_start(BT_LE_SCAN_ACTIVE, device_found);
    LOG_INF("Scan start (slave central) rc=%d", err);
}

/* --- API used by bt_connect.c ------------------------------------------------
 */

bool ykb_slave_is_connected(void) {
    return ykb_connected && keys_rx_handle != 0;
}

/* Send 8-byte report upstream to the master using Write Without Response */
void ykb_slave_send_keys(const uint8_t data[8]) {
    if (!master_conn || keys_rx_handle == 0) {
        return;
    }

    /* Optional: drop duplicates to reduce traffic */
    static uint8_t last_sent[8];
    if (memcmp(last_sent, data, 8) == 0) {
        return;
    }

    int rc = bt_gatt_write_without_response(master_conn, keys_rx_handle, data,
                                            8, false);
    if (rc == 0) {
        memcpy(last_sent, data, 8);
    } else {
        LOG_WRN("WNR to master rc=%d", rc);
    }
}
