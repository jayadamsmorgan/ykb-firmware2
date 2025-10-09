#ifndef BT_CONNECT_INTER_KB_COMM_H_
#define BT_CONNECT_INTER_KB_COMM_H_

#include <stdint.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/uuid.h>

extern const uint8_t ykb_svc_uuid_le[16];

extern struct bt_uuid_128 YKB_SPLIT_SVC_UUID;
extern struct bt_uuid_128 YKB_KEYS_CHRC_UUID;

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

static bt_addr_le_t slave_id = {
    .type = BT_ADDR_LE_RANDOM,
    .a = {.val = {0x34, 0x12, 0xCE, 0xFA, 0xDE, 0xC0}} /* = C0:DE:FA:CE:12:34 */
};

// L2CAP server adress
#define YKB_L2CAP_PSM 0x0080
// L2CAP max size in bytes
#define YKB_L2CAP_MTU 8

#endif // BT_CONNECT_INTER_KB_COMM_H_
