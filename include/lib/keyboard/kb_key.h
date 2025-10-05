#ifndef LIB_KB_HANDLE_KEY_H_
#define LIB_KB_HANDLE_KEY_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t index; // index of the key in the mapping table
    uint8_t value; // percentage pressed
    bool pressed;
} kb_key_t;

#endif // LIB_KB_HANDLE_KEY_H_
