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

        if (params->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
            /* После прохода по характеристикам — если у нас есть
               state_tx_handle, то запускаем поиск CCC для него */
            if (state_tx_handle) {
                memset(&disc, 0, sizeof(disc));
                disc.uuid = BT_UUID_GATT_CCC;
                disc.start_handle = state_tx_handle + 1;
                disc.end_handle = svc_end;
                disc.type = BT_GATT_DISCOVER_DESCRIPTOR;
                disc.func = discover_cb;
                int rc = bt_gatt_discover(c, &disc);
                LOG_INF("Discover CCC rc=%d (searching after handle 0x%04x)",
                        rc, state_tx_handle);
            } else {
                LOG_WRN("STATE_TX characteristic not found in service");
            }
        } else if (params->type == BT_GATT_DISCOVER_DESCRIPTOR) {
            /* Descriptor discovery finished */
            if (state_ccc_handle) {
                /* подписка уже сделана в handler при нахождении дескриптора */
            } else {
                LOG_WRN("CCC for STATE_TX not found");
            }
        }

        return BT_GATT_ITER_STOP;
    }

    if (params->type == BT_GATT_DISCOVER_PRIMARY) {
        const struct bt_gatt_service_val *prim = attr->user_data;
        svc_start = attr->handle + 1;
        svc_end = prim->end_handle;

        /* Теперь запускаем discovery всех характеристик в сервисе */
        memset(&disc, 0, sizeof(disc));
        disc.uuid = NULL; /* NULL — вернуть все характеристики */
        disc.start_handle = svc_start;
        disc.end_handle = svc_end;
        disc.type = BT_GATT_DISCOVER_CHARACTERISTIC;
        disc.func = discover_cb;
        bt_gatt_discover(c, &disc);
        return BT_GATT_ITER_STOP;
    }

    if (params->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
        const struct bt_gatt_chrc *chrc = attr->user_data;
        if (!chrc) {
            return BT_GATT_ITER_CONTINUE;
        }

        /* Явная проверка UUID'ов */
        if (bt_uuid_cmp(chrc->uuid, &YKB_KEYS_RX_UUID.uuid) == 0) {
            keys_rx_handle = chrc->value_handle;
            LOG_DBG("Found KEYS_RX handle 0x%04x", keys_rx_handle);
        } else if (bt_uuid_cmp(chrc->uuid, &YKB_STATE_TX_UUID.uuid) == 0) {
            state_tx_handle = chrc->value_handle;
            LOG_DBG("Found STATE_TX handle 0x%04x", state_tx_handle);
        } else {
            LOG_DBG("Other characteristic found (handle 0x%04x)",
                    chrc->value_handle);
        }

        /* Продолжаем, чтобы собрать все характеристики */
        return BT_GATT_ITER_CONTINUE;
    }

    if (params->type == BT_GATT_DISCOVER_DESCRIPTOR) {
        char ustr[37];
        if (attr && attr->uuid) {
            bt_uuid_to_str(attr->uuid, ustr, sizeof(ustr));
            LOG_INF("Descriptor handle=0x%04x uuid=%s", attr->handle, ustr);
        } /* Ожидаем найти CCC дескриптор для STATE_TX */
        if (attr->uuid && bt_uuid_cmp(attr->uuid, BT_UUID_GATT_CCC) == 0) {
            state_ccc_handle = attr->handle;
            LOG_INF("Found STATE_TX CCC handle 0x%04x", state_ccc_handle);

            /* Подписываемся */
            memset(&sub, 0, sizeof(sub));
            sub.ccc_handle = state_ccc_handle;
            sub.value_handle = state_tx_handle;
            sub.notify = state_notify_cb;
            sub.value = BT_GATT_CCC_NOTIFY;
            int rc = bt_gatt_subscribe(c, &sub);
            if (rc == 0) {
                LOG_INF("Subscribed to STATE_TX notify (value_handle=0x%04x, "
                        "ccc=0x%04x)",
                        sub.value_handle, sub.ccc_handle);
            } else {
                LOG_ERR("Failed to subscribe STATE_TX (rc=%d)", rc);
            }
        }

        return BT_GATT_ITER_CONTINUE;
    }

    return BT_GATT_ITER_CONTINUE;
}
static uint8_t discover_cb_debug(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr,
                                 struct bt_gatt_discover_params *params) {
    char ustr[BT_UUID_STR_LEN];

    if (!attr) {
        LOG_INF("Discovery finished (type=%u)", params->type);
        return BT_GATT_ITER_STOP;
    }

    if (params->type == BT_GATT_DISCOVER_PRIMARY) {
        const struct bt_gatt_service_val *prim = attr->user_data;
        svc_start = attr->handle + 1;
        svc_end = prim->end_handle;
        LOG_INF("Service range: 0x%04x - 0x%04x", svc_start, svc_end);

        /* Запросим все характеристики в этом сервисе */
        memset(&disc, 0, sizeof(disc));
        disc.uuid = NULL;
        disc.func = discover_cb_debug;
        disc.start_handle = svc_start;
        disc.end_handle = svc_end;
        disc.type = BT_GATT_DISCOVER_CHARACTERISTIC;
        bt_gatt_discover(conn, &disc);
        return BT_GATT_ITER_STOP;
    }

    if (params->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
        const struct bt_gatt_chrc *chrc = attr->user_data;
        bt_uuid_to_str(chrc->uuid, ustr, sizeof(ustr));
        LOG_INF("Char decl handle=0x%04x value_handle=0x%04x uuid=%s",
                attr->handle, chrc->value_handle, ustr);
        /* продолжать — чтобы собрать все характеристики */
        return BT_GATT_ITER_CONTINUE;
    }

    if (params->type == BT_GATT_DISCOVER_DESCRIPTOR) {
        /* Здесь attr->uuid может быть NULL для некоторых платформ, но обычно
         * есть */
        if (attr->uuid) {
            bt_uuid_to_str(attr->uuid, ustr, sizeof(ustr));
            LOG_INF("Descriptor handle=0x%04x uuid=%s", attr->handle, ustr);
        } else {
            LOG_INF("Descriptor handle=0x%04x uuid=NULL", attr->handle);
        }
        return BT_GATT_ITER_CONTINUE;
    }

    return BT_GATT_ITER_CONTINUE;
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
        .interval_min = 12,
        .interval_max = 24,
        .latency = 4,
        .timeout = 600, /* 4 s */
    };

    bt_le_scan_stop();
    int rc = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, &conn_param,
                               &master_conn);
    if (rc) {
        LOG_ERR("bt_conn_le_create rc=%d", rc);
        bt_le_scan_start(BT_LE_SCAN_ACTIVE, device_found);
        return;
    }
}

static void peer_connected(struct bt_conn *conn, uint8_t err) {
    if (err) {
        LOG_ERR("Central connect failed: 0x%02x %s", err,
                bt_hci_err_to_str(err));
        master_conn = NULL;
        bt_le_scan_start(BT_LE_SCAN_ACTIVE, device_found);
        return;
    }

    LOG_INF("Connected to master as CENTRAL");

    int sec_rc = bt_conn_set_security(conn, BT_SECURITY_L2);
    if (sec_rc) {
        LOG_DBG("bt_conn_set_security rc=%d", sec_rc);
        /* можно попытаться discovery сразу, если peripheral не требует security
         */
        start_discovery(conn);
    } else {
        /* дождёмся security_changed и запустим discovery там */
    }
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
static void security_changed(struct bt_conn *conn, bt_security_t level,
                             enum bt_security_err err) {
    if (conn != master_conn)
        return;
    if (err) {
        LOG_ERR("security failed: %d", err);
        return;
    }
    if (level >= BT_SECURITY_L2) {
        LOG_INF("security OK, starting discovery");
        ykb_connected = true;
        start_discovery(conn);
    }
}
BT_CONN_CB_DEFINE(slave_central_cb) = {
    .connected = peer_connected,
    .disconnected = peer_disconnected,
    .security_changed = security_changed,
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
