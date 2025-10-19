#ifndef INTER_KB_PROTO_H
#define INTER_KB_PROTO_H

#include <stdint.h>

#include <zephyr/kernel.h>

#define INTER_KB_PROTO_V1 0x01

#define IS_INTER_KB_PROTO_V(X) (X == INTER_KB_PROTO_V1)

#define CONFIG_INTER_KB_COMM_PROTO_MAX_LEN 10

#define INTER_KB_PROTO_DATA_TYPE_KEYS 1
#define INTER_KB_PROTO_DATA_TYPE_KB_SETTINGS 2
#define INTER_KB_PROTO_DATA_TYPE_BL_STATE 3
// TODO more

#define IS_INTER_KB_PROTO_DATA_TYPE(X)                                         \
    (X == INTER_KB_PROTO_DATA_TYPE_KEYS ||                                     \
     X == INTER_KB_PROTO_DATA_TYPE_KB_SETTINGS ||                              \
     X == INTER_KB_PROTO_DATA_TYPE_BL_STATE)

// To use both ways master->slave & slave->master
struct inter_kb_proto {
    uint8_t version;   // Version in case this protocol struct changes
    uint8_t data_type; // Specify which data we are transfering to the receiver
                       // (1 = keys, 2 = state, etc)
    uint8_t data[CONFIG_INTER_KB_COMM_PROTO_MAX_LEN];
} __packed;

// Fill out 'out' with data
//
// Returns the size of data needed to be transfered or negative value on error
//
// Example to send data to receiver:
//
// ```
//  uint32_t keys[2] = { 0b1010101101, 0b00000000 };
//  struct inter_kb_proto protocol = {0};
//  int res = inter_kb_proto_new(INTER_KB_PROTO_DATA_TYPE_KEYS, keys, 8,
//                              &protocol);
//  if (res <= 0) {
//      return;
//  }
//
//  // Then we send with size 'res' instead of sizeof(struct inter_kb_proto):
//  send_data_to_receiver((uint8_t *)&protocol, res);
//
// ```
static inline int inter_kb_proto_new(uint8_t data_type, void *data,
                                     size_t data_len,
                                     struct inter_kb_proto *out) {
    if (data_len > CONFIG_INTER_KB_COMM_PROTO_MAX_LEN) {
        // Too much data
        return -1;
    }
    if (!out || !data || data_len == 0) {
        // Bad args
        return -2;
    }

    out->version = INTER_KB_PROTO_V1;
    out->data_type = data_type;
    memcpy(out->data, data, data_len);

    return data_len + 2;
}

// Parse incoming bytes on the receiver side
//
// Returns the size of the actual data 'out->data'
//
// Example to parse data on the receiver:
//
// ```
//  uint8_t *data; // Some data we got on receive
//  size_t data_len; // Size of the data we got
//
//  struct inter_kb_proto protocol = {0};
//  int res = inter_kb_proto_parse(data, data_len, &protocol);
//  if (res <= 0) {
//      return;
//  }
//
//  // Do something with the actual data:
//
//  if (protocol.data_type == INTER_KB_PROTO_DATA_TYPE_KEYS) {
//      uint32_t keys[2];
//      memcpy(keys, protocol.data, protocol.data_len);
//      some_func_to_handle_incoming_keys(keys);
//  }
// ```
static inline int inter_kb_proto_parse(uint8_t *data, size_t data_len,
                                       struct inter_kb_proto *out) {
    if (data_len > sizeof(struct inter_kb_proto)) {
        // Too much data to handle
        return -1;
    }
    if (data_len < 2) {
        // Insufficient data size
        return -2;
    }
    if (!out || !data) {
        // Bad args
        return -3;
    }

    if (!IS_INTER_KB_PROTO_V(data[0])) {
        // Unsupported protocol version
        return -4;
    }
    out->version = data[0];

    if (!IS_INTER_KB_PROTO_DATA_TYPE(data[1])) {
        // Unsupported data type
        return -5;
    }
    out->data_type = data[1];

    if (data_len == 2) {
        return 0;
    }

    memcpy(out->data, &data[2], data_len - 2);

    return data_len - 2;
}

#endif // INTER_KB_PROTO_H
