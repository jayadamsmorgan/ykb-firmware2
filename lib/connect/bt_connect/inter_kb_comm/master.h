#ifndef BT_CONNECT_MASTER_H_
#define BT_CONNECT_MASTER_H_

#include <lib/connect/bt_connect.h>

int ykb_master_state_notify(const uint8_t payload[8]);

void ykb_master_merge_reports(uint8_t report[BT_CONNECT_HID_REPORT_COUNT],
                              uint8_t report_count);

#endif // BT_CONNECT_MASTER_H_
