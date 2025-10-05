#ifndef LIB_KB_HANDLE_KEY_H_
#define LIB_KB_HANDLE_KEY_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t index; // index of the key in the mapping table
    uint8_t code;  // HID code
    uint8_t value; // percentage pressed
    bool pressed;
} kb_key_t;

static inline bool kb_key_equal(kb_key_t *lhs, kb_key_t *rhs) {
    if (lhs == NULL || rhs == NULL)
        return false;

    if (lhs == rhs)
        return true;

    return lhs->code == rhs->code && lhs->index == rhs->index;
}

#endif // LIB_KB_HANDLE_KEY_H_
