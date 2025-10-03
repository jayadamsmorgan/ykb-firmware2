// SPDX-License-Identifier: Apache-2.0

#ifndef LIB_BT_CONNECT_H_
#define LIB_BT_CONNECT_H_

#include <stdbool.h>
#include <stdint.h>

#define BT_CONNECT_HID_REPORT_COUNT 8

int bt_connect_init();

void bt_connect_send(uint8_t report[BT_CONNECT_HID_REPORT_COUNT],
                     uint8_t report_size);

bool bt_connect_is_ready();

void bt_connect_start_advertising();

void bt_connect_factory_reset();

void bt_connect_set_battery_charging();
void bt_connect_set_battery_discharging();
void bt_connect_set_battery_disconnected();
void bt_connect_set_battery_connected();
void bt_connect_set_battery_level(uint8_t level);

#endif // LIB_BT_CONNECT_H_
