#ifndef BT_CONNECT_MASTER_H_
#define BT_CONNECT_MASTER_H_

#include <lib/connect/bt_connect.h>

void ykb_master_link_start();

void ykb_master_link_stop();

void ykb_master_merge_reports(uint8_t report[BT_CONNECT_HID_REPORT_COUNT],
                              uint8_t report_count);

#endif // BT_CONNECT_MASTER_H_
