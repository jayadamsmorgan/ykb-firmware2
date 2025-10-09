#include "master.h"

#include "inter_kb_comm.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(bt_connect, CONFIG_BT_CONNECT_LOG_LEVEL);

static struct bt_conn *host_conn = NULL;
static struct bt_conn *slave_conn = NULL;

static uint8_t slave_report[BT_CONNECT_HID_REPORT_COUNT] = {0};

static void start_advertising(void) {
    int err;
    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd,
                          ARRAY_SIZE(sd));
    if (err == -EALREADY) {
        LOG_ERR("Advertising already started");
    } else if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
    } else {
        LOG_INF("Advertising successfully (re)started");
    }
}

static void ykb_master_connected(struct bt_conn *conn, uint8_t err) {

    char addr[BT_ADDR_LE_STR_LEN];

    // Get information about central device
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    struct bt_conn_info conn_info;

    int res = bt_conn_get_info(conn, &conn_info);
    if (res) {
        LOG_ERR("Unable to get connection info (err %d)", res);
        return;
    }

    // Check if it slave or not
    // if not, we consider it a host
}

static void ykb_master_disconnected(struct bt_conn *conn, uint8_t reason) {
    char addr[BT_ADDR_LE_STR_LEN];
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

BT_CONN_CB_DEFINE(ykb_master_conn_callbacks) = {
    .connected = ykb_master_connected,
    .disconnected = ykb_master_disconnected,
    .security_changed = security_changed,
};

/*-------------L2CAP--------------------*/

// Channel for L2CAP connection
static struct bt_l2cap_le_chan ykb_l2cap_slave_chan;

// --- L2CAP Callbacks и Сервер для слейва ---
static int ykb_l2cap_recv(struct bt_l2cap_chan *chan, struct net_buf *buf) {
    // Получены данные от слейв-платы
    if (buf->len == BT_CONNECT_HID_REPORT_COUNT) {
        memcpy(slave_report, buf->data, BT_CONNECT_HID_REPORT_COUNT);
        LOG_DBG("Received slave report: %s",
                bt_hex(slave_report, BT_CONNECT_HID_REPORT_COUNT));
    } else {
        LOG_WRN("Received L2CAP data of unexpected length: %u", buf->len);
    }
    net_buf_unref(buf);
}

static void ykb_l2cap_connected(struct bt_l2cap_chan *chan) {
    LOG_INF("L2CAP channel connected from slave");
    // Соединение L2CAP успешно установлено.
    // Теперь можно отправлять данные на мастер-плату.
}

static void ykb_l2cap_disconnected(struct bt_l2cap_chan *chan) {
    LOG_INF("L2CAP channel disconnected from slave");
    // L2CAP канал разорван.
    // Нужно убедиться, что slave_conn обнуляется при отключении базового
    // BLE-соединения
}

// Операции для L2CAP канала
static struct bt_l2cap_chan_ops l2cap_slave_ops = {
    .connected = ykb_l2cap_connected,
    .disconnected = ykb_l2cap_disconnected,
    .recv = ykb_l2cap_recv,
};

// Callback для принятия входящего L2CAP-канала
static int ykb_l2cap_accept(struct bt_conn *conn,
                            struct bt_l2cap_server *server,
                            struct bt_l2cap_chan **chan) {
    // Проверяем, это ли соединение от слейва
    if (conn != slave_conn) {
        LOG_INF("Accepting L2CAP connection (conn %p, current slave_conn %p)",
                conn, slave_conn);
        slave_conn =
            bt_conn_ref(conn); // Обновляем ссылку на соединение со слейвом
    }

    *chan = &ykb_l2cap_slave_chan;
    bt_l2cap_le_chan_init(&ykb_l2cap_slave_chan, &l2cap_slave_ops);
    return 0;
}

// L2CAP сервер
static struct bt_l2cap_server l2cap_ykb_server = {
    .psm = YKB_L2CAP_PSM,
    .accept = ykb_l2cap_accept,
};

// Функция инициализации L2CAP сервера
void ykb_master_l2cap_server_init(void) {
    int err = bt_l2cap_server_register(&l2cap_ykb_server);
    if (err) {
        LOG_ERR("Failed to register L2CAP server (err %d)", err);
    } else {
        LOG_INF("L2CAP server registered, PSM: %d", YKB_L2CAP_PSM);
    }
}
/*-------------Other STUFF--------------------*/

void ykb_master_merge_reports(uint8_t report[BT_CONNECT_HID_REPORT_COUNT],
                              uint8_t report_count) {
    report[0] |= slave_report[0];
    if (report_count == 6) {
        return;
    }
    uint8_t j = 2;
    for (uint8_t i = report_count + 2; i < 8; ++i) {
        if (report[i] == 0) {
            if (slave_report[j] == 0)
                break;
            report[i] = slave_report[j];
            j++;
        }
    }
}
