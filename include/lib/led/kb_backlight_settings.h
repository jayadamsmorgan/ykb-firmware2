#ifndef KB_BACKLIGHT_SETTINGS_H
#define KB_BACKLIGHT_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t version;

    uint16_t mode_idx;
    float mode_speed;
    uint8_t brightness;
    bool on;

} backlight_state_img;

// Increment every time backlight_state_img changes
#define KB_BL_SETTINGS_IMAGE_VERSION 1

void kb_backlight_settings_init(void);
void kb_bl_settings_save(void);

void kb_bl_settings_load_from_image(backlight_state_img *img);

void kb_backlight_settings_build_image_from_runtime(backlight_state_img *img);

#endif // KB_BACKLIGHT_SETTINGS_H
