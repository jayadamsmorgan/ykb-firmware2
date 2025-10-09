#include "slave.h"

#include "inter_kb_comm.h"
#include <lib/connect/bt_connect.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/uuid.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
LOG_MODULE_DECLARE(bt_connect, CONFIG_BT_CONNECT_LOG_LEVEL);

static uint8_t ykb_ccc_enabled;

static struct bt_conn *ykb_master_conn = NULL;

static struct bt_l2cap_le_chan ykb_l2cap_master_chan;

static atomic_t conn_connected = ATOMIC_INIT(0);
static atomic_t chan_connected = ATOMIC_INIT(0);

static struct bt_gatt_discover_params disc_params;
static struct bt_gatt_subscribe_params sub_params;

/* --- Central scan/connect to master ------------------------*/
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

// ykb_device_found is for scanning for device and establishing a connection
static void ykb_device_found(const bt_addr_le_t *addr, int8_t rssi,
                             uint8_t adv_type, struct net_buf_simple *ad) {

    LOG_INF("Found some BT device");
    // Check if connection have right one UUID
    if (!adv_has_split_uuid(ad)) {
        return;
    }

    // Check if master already connected
    if (ykb_master_conn != NULL) {
        return;
    }

    LOG_INF("Found master keyboard. Attempting to connect.");

    struct bt_le_conn_param conn_param = {
        .interval_min = 6,
        .interval_max = 12,
        .latency = 0,
        .timeout = 400,
    };

    bt_le_scan_stop();
    int rc = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, &conn_param,
                               &ykb_master_conn);
    if (rc) {
        LOG_ERR("Failed to create connection to master (err %d)", rc);
        ykb_master_conn = NULL;
        start_scanning();
    }
}

/* --- Helper to start/restart scanning ------------------------*/

void start_scanning(void) {
    int err = bt_le_scan_start(BT_LE_SCAN_ACTIVE, ykb_device_found);
    if (err &&
        err != -EALREADY) { // -EALREADY means that we are alreday scanning
        LOG_ERR("Scan failed to start (err %d)", err);
    } else if (err == 0) {
        LOG_INF("Scanning successfully (re)started");
    }
}

// --- L2CAP Callbacks ---

static int ykb_l2cap_recv(struct bt_l2cap_chan *chan, struct net_buf *buf) {
    LOG_DBG("Received L2CAP data from master, len: %u", buf->len);
    // net_buf_unref(buf);
    return 0;
}

static void ykb_l2cap_connected(struct bt_l2cap_chan *chan) {
    LOG_INF("L2CAP channel connected to master!");
}

static void ykb_l2cap_disconnected(struct bt_l2cap_chan *chan) {
    LOG_INF("L2CAP channel disconnected from master!");
}
static void ykb_l2cap_sent(struct bt_l2cap_chan *chan) {
    LOG_INF("ykb_l2cap_sended");
}

// Операции L2CAP
static struct bt_l2cap_chan_ops l2cap_master_ops = {
    .sent = ykb_l2cap_sent,
    .connected = ykb_l2cap_connected,
    .disconnected = ykb_l2cap_disconnected,
    .recv = ykb_l2cap_recv,
};

// --- Connection Callback  ---
static void ykb_slave_connected(struct bt_conn *conn, uint8_t err) {
    char addr_str[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr_str, sizeof(addr_str));

    if (err) {
        LOG_ERR("Failed to connect to master %s, err 0x%02x %s", addr_str, err,
                bt_hci_err_to_str(err));
        ykb_master_conn = NULL;
        start_scanning();
        return;
    }

    LOG_INF("Connected to master: %s", addr_str);
    ykb_master_conn = bt_conn_ref(conn);

    int rc =
        bt_l2cap_chan_connect(conn, &ykb_l2cap_master_chan.chan, YKB_L2CAP_PSM);
    if (rc) {
        LOG_ERR("Failed to connect L2CAP channel (err %d)", rc);
        // bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    }
}

static void ykb_slave_disconnected(struct bt_conn *conn, uint8_t reason) {
    char addr_str[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr_str, sizeof(addr_str));

    LOG_INF("Disconnected from master %s, reason 0x%02x %s", addr_str, reason,
            bt_hci_err_to_str(reason));

    if (conn == ykb_master_conn) {
        bt_conn_unref(ykb_master_conn);
        ykb_master_conn = NULL;
        LOG_WRN("Master disconnected, rescanning...");
        start_scanning(); // Перезапускаем сканирование для поиска мастера
    }
    // Здесь также можно добавить логику для очистки сопряженных устройств
    // if (reason == BT_HCI_ERR_REMOTE_USER_TERM_CONN) { /* ... */ }
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
                             enum bt_security_err err) {
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (!err) {
        LOG_INF("Security changed: %s level %u", addr, level);
        // Если безопасность установлена, и это наше соединение с мастером,
        // можно убедиться, что L2CAP-канал уже установлен или инициировать его.
    } else {
        LOG_ERR("Security failed: %s level %u err %s(%d)", addr, level,
                bt_security_err_to_str(err), err);
        // Если безопасность не установлена, возможно, соединение нужно
        // разорвать и попробовать снова.
        bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
    }
}

// Callbacks for Bluetooth connection
BT_CONN_CB_DEFINE(ykb_slave_conn_callbacks) = {
    .connected = ykb_slave_connected,
    .disconnected = ykb_slave_disconnected,
    .security_changed = security_changed,
};

// --- Slave Public API ---

bool ykb_slave_is_connected() {
    return atomic_get(&conn_connected) && atomic_get(&chan_connected);
}

void ykb_slave_send_keys(const uint8_t data[BT_CONNECT_HID_REPORT_COUNT]) {

    if (!ykb_slave_is_connected()) {
        LOG_WRN("Slave not connected to master, cannot send keys.");
        return;
    }

    // Отправляем данные по L2CAP-каналу
    // bt_l2cap_chan_send принимает struct bt_l2cap_chan *, net_buf *, а не raw
    // data. Нужно создать net_buf для отправки.

    // struct net_buf *buf = bt_l2cap_create_pdu_timeout(
    //     NULL, 0, K_FOREVER); // K_FOREVER или другой таймаут
    // if (!buf) {
    //     LOG_ERR("Failed to create L2CAP PDU buffer");
    //     return;
    // }

    // net_buf_add_mem(buf, data, BT_CONNECT_HID_REPORT_COUNT); // Добавляем
    // данные
    //
    // int rc = bt_l2cap_chan_send(&ykb_l2cap_master_chan.chan, buf);
    // if (rc) {
    //     LOG_ERR("Failed to send L2CAP data (err %d)", rc);
    //     net_buf_unref(buf); // Освобождаем буфер, если отправка не удалась
    // } else {
    //     LOG_DBG("L2CAP data sent to master.");
    // }
}

void ykb_slave_link_start() {
    start_scanning();
}

void ykb_slave_link_stop() {
    bt_le_scan_stop();
}
