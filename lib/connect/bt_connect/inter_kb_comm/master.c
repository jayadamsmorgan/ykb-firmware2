#include "inter_kb_comm.h"

#include <lib/connect/bt_connect.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>

#include <string.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(bt_connect, CONFIG_BT_CONNECT_LOG_LEVEL);

void adv_periodic_resume(bool immediate);
void adv_periodic_pause(void);
void ykb_master_l2cap_server_init(void);
#define YKB_L2CAP_PSM 0x0080
// L2CAP max size in bytes
#define YKB_L2CAP_MTU 8

K_MUTEX_DEFINE(report_mutex); /* статически инициализированный mutex */

/* --- Master’s YKB Split Service (Peripheral) ---
 *
 * - KEYS_RX (UUID ...0002): Write Without Response (8 bytes)
 *   Slave (central) sends its HID 8-byte report here.
 *
 * - STATE_TX (UUID ...0003): Notify (8 bytes)
 *   Master notifies LED/mode info down to slave.
 */
static struct bt_conn *host_conn = NULL;
static struct bt_conn *slave_conn = NULL;

static atomic_t host_connected = ATOMIC_INIT(0);

static atomic_t slave_conn_connected = ATOMIC_INIT(0);
static atomic_t slave_chan_connected = ATOMIC_INIT(0);

static atomic_t slave_connection = ATOMIC_INIT(0);
static uint8_t last_slave_report[8];
static uint8_t state_tx_value[8];
static uint8_t ykb_ccc_enabled;

// static ssize_t keys_rx_write(struct bt_conn *conn,
//                              const struct bt_gatt_attr *attr, const void
//                              *buf, uint16_t len, uint16_t offset, uint8_t
//                              flags) {
//     if (offset != 0 || len != 8) {
//         LOG_WRN("Unexpected KEYS_RX len=%u", len);
//         return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
//     }
//     LOG_INF("Recive some buffer from slave");
//     k_mutex_lock(&report_mutex, K_MSEC(50));
//     memcpy(last_slave_report, buf, 8);
//     k_mutex_unlock(&report_mutex);
//
//     return len;
// }

// static void state_ccc_cfg_changed(const struct bt_gatt_attr *attr,
//                                   uint16_t value) {
//     LOG_INF("ykb_ccc_cfg_changed");
//     ykb_ccc_enabled = (value == BT_GATT_CCC_NOTIFY);
// }
/* Attribute layout:
 * 0: Primary Service
 * 1: Char Decl (KEYS_RX)
 * 2: Char Value (KEYS_RX)
 * 3: Char Decl (STATE_TX)
 * 4: Char Value (STATE_TX)
 * 5: CCC for STATE_TX
 */
// BT_GATT_SERVICE_DEFINE(
//     ykb_split_svc, BT_GATT_PRIMARY_SERVICE(&YKB_SPLIT_SVC_UUID),
//
//     /* KEYS_RX: central writes 8B to us (Write Without Response) */
//     BT_GATT_CHARACTERISTIC(&YKB_KEYS_RX_UUID.uuid,
//                            BT_GATT_CHRC_WRITE_WITHOUT_RESP,
//                            BT_GATT_PERM_WRITE, NULL, keys_rx_write, NULL),
//
//     /* STATE_TX: we notify slave (8B payload) */
//     BT_GATT_CHARACTERISTIC(&YKB_STATE_TX_UUID.uuid, BT_GATT_CHRC_NOTIFY,
//                            BT_GATT_PERM_NONE, NULL, NULL, state_tx_value),
//     BT_GATT_CCC(state_ccc_cfg_changed, BT_GATT_PERM_READ |
//     BT_GATT_PERM_WRITE));
//
// /* Push state (e.g., LED/CapsLock) down to slave (optional helper) */
// int ykb_master_state_notify(const uint8_t payload[8]) {
//     memcpy(state_tx_value, payload, 8);
//     /* value handle for STATE_TX is ykb_split_svc.attrs[4] */
//     return bt_gatt_notify(NULL, &ykb_split_svc.attrs[4], state_tx_value, 8);
// }

/* Merge the slave’s last 8-byte report into 'report' (local) before sending to
 * host */
void ykb_master_merge_reports(uint8_t report[BT_CONNECT_HID_REPORT_COUNT],
                              uint8_t report_size) {
    /* OR in modifier byte */
    k_mutex_lock(&report_mutex, K_MSEC(50));
    report[0] |= last_slave_report[0];

    /* If NKRO isn't used, fill empty slots with slave keycodes (2..7) */
    if (report_size == 6) {
        return; /* 6 = only modifiers + 6 keys already in use by your local half
                 */
    }

    uint8_t j = 2;
    for (uint8_t i = report_size + 2; i < 8; ++i) {
        if (report[i] == 0) {
            if (last_slave_report[j] == 0)
                break;
            /* Avoid duplicates */
            bool dup = false;
            for (uint8_t k = 2; k < 8; ++k) {
                if (report[k] == last_slave_report[j]) {
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                report[i] = last_slave_report[j];
            }
            j++;
            if (j >= 8)
                break;
        }
    }
    k_mutex_unlock(&report_mutex);
}

#include <stdatomic.h>

static atomic_t periph_conn_count = ATOMIC_INIT(0);

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

static void try_resume_advertising(void) {
    /* If we still have spare connection objects, resume advertising. */
    if (atomic_get(&periph_conn_count) < CONFIG_BT_MAX_CONN - 2) {
        int rc = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), sd,
                                 ARRAY_SIZE(sd));
        if (rc == -EALREADY) {
            LOG_WRN("adv_resuming failed: adv already going");
        } else if (rc) {
            LOG_WRN("adv resume failed:  rc=%d", rc);
        } else {
            LOG_INF("adv resumed successfuly");
        }
    } else {
        LOG_INF("Already MAX conenctions: %d", atomic_get(&periph_conn_count));
    }
}

static void conn_connected(struct bt_conn *conn, uint8_t err) {

    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info)) {
        return;
    }

    if (!err) {
        atomic_inc(&periph_conn_count);
        /* Сохраняем ссылку, если хотим держать глобально */
        // int er = bt_le_adv_stop();
        // if (er)
        //     LOG_INF("Failed to stop advertising: rc= %d", er);
        adv_periodic_pause();

        LOG_INF("Seems like we have a connection");
        int sec_rc = bt_conn_set_security(conn, BT_SECURITY_L2);
        if (sec_rc) {
            LOG_DBG("bt_conn_set_security rc=%d", sec_rc);
        }
        return;
    }
    LOG_INF("Someone want to connect but we refuse because: %d", err);
}

static void conn_disconnected(struct bt_conn *conn, uint8_t reason) {
    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info)) {
        return;
    }
    char addr[BT_ADDR_LE_STR_LEN];
    // bt_conn_unref(conn);
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Disconnected from %s, reason 0x%02x %s", addr, reason,
            bt_hci_err_to_str(reason));
    if (conn == host_conn) {
        bt_conn_unref(host_conn);
        host_conn = NULL;
    }
    if (conn == slave_conn) {
        bt_conn_unref(slave_conn);
        slave_conn = NULL;
    }
}

static int find_conn_info(struct bt_conn *conn, void *data) {
    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info) == 0) {
        char addr[BT_ADDR_LE_STR_LEN];
        bt_addr_le_to_str(info.le.remote, addr, sizeof(addr));
        printk("conn %p state=%u role=%u id=%u remote=%s\n", conn, info.state,
               info.role, info.id, addr);
    } else {
        printk("conn %p (no info)\n", conn);
    }
    return 0;
}
static void dump_conns(void) {
    struct bt_conn *c;
    LOG_INF("=== dump_conns ===\n");
    bt_conn_foreach(BT_CONN_TYPE_LE, find_conn_info, NULL);
    printk("=== end dump ===\n");
}
static void security_changed(struct bt_conn *conn, bt_security_t level,
                             enum bt_security_err err) {
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    static const struct bt_le_conn_param preferred = {
        .interval_min = 10, // 15 ms
        .interval_max = 40, // 30 ms
        .latency = 4,
        .timeout = 1000, // 4 s
    };
    int rc = bt_conn_le_param_update(conn, &preferred);
    if (rc) {
        LOG_ERR("Conn param update failed: %d", rc);
    }
    if (err) {
        LOG_ERR("Security failed for %s: level %u err %s", addr, level,
                bt_hci_err_to_str(err));
        dump_conns();

        if (conn == host_conn) {
            bt_conn_unref(host_conn);
            host_conn = NULL;
        }
        if (conn == slave_conn) {
            bt_conn_unref(slave_conn);
            slave_conn = NULL;
        }
        return;
    }

    if (!slave_conn) {
        slave_conn = bt_conn_ref(conn);
    } else if (!host_conn) {
        host_conn = bt_conn_ref(conn);
    }
    LOG_INF("Security level updated for %s: level %u", addr, level);

    if (atomic_get(&periph_conn_count) < CONFIG_BT_MAX_CONN)
        adv_periodic_resume(false);
    if (level >= BT_SECURITY_L2) {
    }
}
static void conn_recycled(void) {
    LOG_INF("Conn recycled; can resume advertising");
    atomic_dec(&periph_conn_count);
    adv_periodic_resume(false);
}
static bool conn_param_req(struct bt_conn *conn,
                           struct bt_le_conn_param *param) {
    LOG_INF("connection request param update");
}
static void conn_param_updated(struct bt_conn *conn, uint16_t interval,
                               uint16_t latency, uint16_t timeout) {
    struct bt_conn_info info;

    int rc = bt_conn_get_info(conn, &info);
    if (rc) {
        LOG_ERR("Failed to get conn info: %d", rc);
        return;
    }

    char addr_str[BT_ADDR_LE_STR_LEN];
    LOG_INF(
        "Connection parameters updated for %s",
        bt_addr_le_to_str(bt_conn_get_dst(conn), addr_str, sizeof(addr_str)));
    LOG_INF("Role: %s",
            info.role == BT_CONN_ROLE_CENTRAL ? "CENTRAL" : "PERIPH");
    LOG_INF("Interval: %u (%ums)", interval, interval * 1.25);
    LOG_INF("Latency: %u", latency);
    LOG_INF("Supervision timeout: %u (%ums)", timeout, timeout * 10);
    LOG_INF("Current interval: %u (%ums)", info.le.interval,
            info.le.interval * 1.25);
    LOG_INF("Current latency: %u", info.le.latency);
    LOG_INF("Current timeout: %u (%ums)", info.le.timeout,
            info.le.timeout * 10);
}

BT_CONN_CB_DEFINE(periph_adv_cb) = {
    .connected = conn_connected,
    .disconnected = conn_disconnected,
    .security_changed = security_changed,
    .recycled = conn_recycled,
    .le_param_req = conn_param_req,
    .le_param_updated = conn_param_updated,
};

#define ADV_PERIOD_MS 5000

static struct k_work_delayable adv_resume_work;
static atomic_t adv_paused = ATOMIC_INIT(0); /* 0 == running, 1 == paused

/* Обработчик delayable work */
static void adv_resume_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    /* Попытка возобновить рекламу */
    try_resume_advertising();

    /* Если не на паузе, запланировать следующий запуск */
    if (!atomic_get(&adv_paused)) {
        k_work_schedule(&adv_resume_work, K_MSEC(ADV_PERIOD_MS));
    } else {
        LOG_DBG("adv periodic paused; not rescheduling");
    }
}

/* Инициализация — вызвать один раз (например в bt_ready или init) */
void adv_periodic_init(void) {
    k_work_init_delayable(&adv_resume_work, adv_resume_work_handler);
}

/* Запустить цикл: первый запуск через ADV_PERIOD_MS (или K_NO_WAIT для
 * немедленно) */
void adv_periodic_start(bool immediate) {
    atomic_set(&adv_paused, 0);
    if (immediate) {
        k_work_schedule(&adv_resume_work, K_NO_WAIT);
    } else {
        k_work_schedule(&adv_resume_work, K_MSEC(ADV_PERIOD_MS));
    }
}

/* Приостановить (останавливает дальнейшие планирования) */
void adv_periodic_pause(void) {
    atomic_set(&adv_paused, 1);
    /* отменим уже запланированную работу, чтобы она не сработала позже */
    (void)k_work_cancel_delayable(&adv_resume_work);
}

/* Возобновить (если был pause) */
void adv_periodic_resume(bool immediate) {
    /* снять паузу и (при нужде) запланировать */
    atomic_set(&adv_paused, 0);
    if (immediate) {
        k_work_schedule(&adv_resume_work, K_NO_WAIT);
    } else {
        k_work_schedule(&adv_resume_work, K_MSEC(ADV_PERIOD_MS));
    }
}

/* Полная остановка */
void adv_periodic_stop(void) {
    atomic_set(&adv_paused, 1);
    (void)k_work_cancel_delayable(&adv_resume_work);
}

void ykb_master_link_start() {
    ykb_master_l2cap_server_init();
    adv_periodic_init();
    adv_periodic_start(true);
}

/*-------------L2CAP--------------------*/

// Channel for L2CAP connection
static struct bt_l2cap_le_chan ykb_l2cap_slave_chan;

// --- L2CAP Callbacks и Сервер для слейва ---
static int ykb_l2cap_recv(struct bt_l2cap_chan *chan, struct net_buf *buf) {
    // Получены данные от слейв-платы
    if (buf->len == BT_CONNECT_HID_REPORT_COUNT) {
        memcpy(last_slave_report, buf->data, BT_CONNECT_HID_REPORT_COUNT);
        LOG_DBG("Received slave report: %s",
                bt_hex(last_slave_report, BT_CONNECT_HID_REPORT_COUNT));
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
    // if (conn != slave_conn) {
    //     LOG_INF("New L2CAP connection but we still have previous
    //     slave_conn");
    // }
    memset(&ykb_l2cap_slave_chan, 0, sizeof(ykb_l2cap_slave_chan));

    /* Важно: вернуть указатель на внутреннее поле .chan */
    ykb_l2cap_slave_chan.chan.ops =
        &l2cap_slave_ops; /* Если структура вложена: .chan.chan */
    /* В зависимости от версии API может быть просто
       ykb_l2cap_slave_chan.chan.ops = &...; проверь, у тебя вложенность:
       bt_l2cap_le_chan.chan (struct bt_l2cap_chan) */

    *chan = &ykb_l2cap_slave_chan.chan;
    LOG_INF("L2CAP connectiont accepted");
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
    // if (err) {
    //     LOG_ERR("Failed to register L2CAP server (err %d)", err);
    // } else {
    //     LOG_INF("L2CAP server registered, PSM: %d", YKB_L2CAP_PSM);
    // }
}
