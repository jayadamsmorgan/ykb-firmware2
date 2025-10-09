#include "inter_kb_comm.h"

#include <lib/connect/bt_connect.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/uuid.h>

#include <string.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(bt_connect, CONFIG_BT_CONNECT_LOG_LEVEL);

/* --- Master’s YKB Split Service (Peripheral) ---
 *
 * - KEYS_RX (UUID ...0002): Write Without Response (8 bytes)
 *   Slave (central) sends its HID 8-byte report here.
 *
 * - STATE_TX (UUID ...0003): Notify (8 bytes)
 *   Master notifies LED/mode info down to slave.
 */

static uint8_t last_slave_report[8];
static uint8_t state_tx_value[8];

static ssize_t keys_rx_write(struct bt_conn *conn,
                             const struct bt_gatt_attr *attr, const void *buf,
                             uint16_t len, uint16_t offset, uint8_t flags) {
    if (offset != 0 || len != 8) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    memcpy(last_slave_report, buf, 8);

    /* If you want immediate host update from here, you can trigger a work item
     * that reads local keys, merges, and calls bt_gatt_notify() on the HID
     * char. For now, bt_connect_send() will call ykb_master_merge_reports()
     * when sending.
     */
    return len;
}

static void state_ccc_cfg_changed(const struct bt_gatt_attr *attr,
                                  uint16_t value) {
    /* Optional: you can track whether the slave subscribed to STATE_TX here */
    LOG_INF("Slave %s STATE_TX notifications",
            (value == BT_GATT_CCC_NOTIFY) ? "enabled" : "disabled");
}

/* Attribute layout:
 * 0: Primary Service
 * 1: Char Decl (KEYS_RX)
 * 2: Char Value (KEYS_RX)
 * 3: Char Decl (STATE_TX)
 * 4: Char Value (STATE_TX)
 * 5: CCC for STATE_TX
 */
BT_GATT_SERVICE_DEFINE(
    ykb_split_svc, BT_GATT_PRIMARY_SERVICE(&YKB_SPLIT_SVC_UUID),

    /* KEYS_RX: central writes 8B to us (Write Without Response) */
    BT_GATT_CHARACTERISTIC(&YKB_KEYS_RX_UUID.uuid,
                           BT_GATT_CHRC_WRITE_WITHOUT_RESP, BT_GATT_PERM_WRITE,
                           NULL, keys_rx_write, NULL),

    /* STATE_TX: we notify slave (8B payload) */
    BT_GATT_CHARACTERISTIC(&YKB_STATE_TX_UUID.uuid, BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE, NULL, NULL, state_tx_value),
    BT_GATT_CCC(state_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

/* Push state (e.g., LED/CapsLock) down to slave (optional helper) */
int ykb_master_state_notify(const uint8_t payload[8]) {
    memcpy(state_tx_value, payload, 8);
    /* value handle for STATE_TX is ykb_split_svc.attrs[4] */
    return bt_gatt_notify(NULL, &ykb_split_svc.attrs[4], state_tx_value, 8);
}

/* Merge the slave’s last 8-byte report into 'report' (local) before sending to
 * host */
void ykb_master_merge_reports(uint8_t report[BT_CONNECT_HID_REPORT_COUNT],
                              uint8_t report_size) {
    /* OR in modifier byte */
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
    if (atomic_get(&periph_conn_count) < CONFIG_BT_MAX_CONN) {
        int rc = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd,
                                 ARRAY_SIZE(sd));
        if (rc && rc != -EALREADY) {
            LOG_WRN("adv resume failed rc=%d", rc);
        } else {
            LOG_INF("adv resumed");
        }
    }
}

static void conn_connected(struct bt_conn *conn, uint8_t err) {
    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info)) {
        return;
    }

    /* Only count when we are the peripheral on this link (host<->master,
     * slave<->master) */
    if (!err && info.role == BT_CONN_ROLE_PERIPHERAL) {
        atomic_inc(&periph_conn_count);
        /* Immediately try to resume advertising for the next central */
        try_resume_advertising();
    }
}

static void conn_disconnected(struct bt_conn *conn, uint8_t reason) {
    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info)) {
        return;
    }

    if (info.role == BT_CONN_ROLE_PERIPHERAL) {
        int newv =
            atomic_sub(&periph_conn_count, 1) - 1; /* returns old value */
        (void)newv;
        /* After a peripheral link drops, resume advertising (if not already) */
        try_resume_advertising();
    }
}

/* Optional: also use .recycled to resume if we ran out of conn objects */
static void conn_recycled(void) {
    try_resume_advertising();
}

BT_CONN_CB_DEFINE(periph_adv_cb) = {
    .connected = conn_connected,
    .disconnected = conn_disconnected,
    .recycled =
        conn_recycled, /* requires a Zephyr version that supports .recycled */
};
