#ifndef BT_CONNECT_SLAVE_H_
#define BT_CONNECT_SLAVE_H_

#include <stdbool.h>
#include <stdint.h>

bool ykb_slave_is_connected();

void ykb_slave_send_keys(const uint8_t data[8]);

#endif // BT_CONNECT_SLAVE_H_
