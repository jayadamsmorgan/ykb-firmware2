// SPDX-License-Identifier: Apache-2.0

#ifndef LIB_BT_CONNECT_H_
#define LIB_BT_CONNECT_H_

#include <stdbool.h>
#include <stdint.h>

#define BT_CONNECT_HID_REPORT_COUNT 8

int bt_connect_init();

void bt_connect_send(uint8_t buffer[BT_CONNECT_HID_REPORT_COUNT]);

bool bt_connect_is_ready();

#endif // LIB_BT_CONNECT_H_
