#ifndef KB_BACKLIGHT_H
#define KB_BACKLIGHT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {

    struct kb_bl_mode *mode;
    uint16_t mode_idx;
    float mode_speed;

    uint8_t brightness;

    bool on;

} backlight_state;

#endif // KB_BACKLIGHT_H
