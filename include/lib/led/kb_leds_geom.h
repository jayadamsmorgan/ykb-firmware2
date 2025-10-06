#ifndef KB_LEDS_GEOM_H
#define KB_LEDS_GEOM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define Q(x) ((kb_fp16)((x) * 256))
#define POS(pos_x, pos_y)                                                      \
    (struct kb_leds_position) {                                                \
        .x = Q(pos_x), .y = Q(pos_y),                                          \
    }
#define KB_Q 8

typedef int16_t kb_fp16;

struct kb_leds_position {
    kb_fp16 x;
    kb_fp16 y;
};

struct kb_leds_geom {
    const struct kb_leds_position *positions;
    size_t positions_count;
};

static inline int32_t kb_leds_geom_sqdist_fp(kb_fp16 ax, kb_fp16 ay, kb_fp16 bx,
                                             kb_fp16 by) {
    // squared distance in Q8.8 (no sqrt)
    int32_t dx = (int32_t)ax - (int32_t)bx;
    int32_t dy = (int32_t)ay - (int32_t)by;
    return dx * dx + dy * dy;
}

#define LEDS_POSITIONS_LEFT left_positions
#define LEDS_POSITIONS_RIGHT right_positions
#define KEY_IDX_TO_LED_IDX_MAP_LEFT key_idx_to_led_idx_map_left
#define KEY_IDX_TO_LED_IDX_MAP_RIGHT key_idx_to_led_idx_map_right

#if CONFIG_YKB_RIGHT
#define LEDS_POSITIONS_DEFINE_LEFT(...)
#define LEDS_POSITIONS_DEFINE_RIGHT(...)                                       \
    static struct kb_leds_position                                             \
        LEDS_POSITIONS_RIGHT[CONFIG_KB_KEY_COUNT * 2] = {__VA_ARGS__}
#define KEY_IDX_TO_LED_IDX_MAP_DEFINE_LEFT(...)
#define KEY_IDX_TO_LED_IDX_MAP_DEFINE_RIGHT(...)                               \
    static uint8_t KEY_IDX_TO_LED_IDX_MAP_RIGHT[CONFIG_KB_KEY_COUNT] = {       \
        __VA_ARGS__}
#define LEDS_POSITIONS LEDS_POSITIONS_RIGHT
#define KEY_IDX_TO_LED_IDX_MAP KEY_IDX_TO_LED_IDX_MAP_RIGHT
#endif // CONFIG_YKB_RIGHT

#if CONFIG_YKB_LEFT
#define LEDS_POSITIONS_DEFINE_LEFT(...)                                        \
    static struct kb_leds_position                                             \
        LEDS_POSITIONS_LEFT[CONFIG_KB_KEY_COUNT * 2] = {__VA_ARGS__}
#define LEDS_POSITIONS_DEFINE_RIGHT(...)
#define KEY_IDX_TO_LED_IDX_MAP_DEFINE_LEFT(...)                                \
    static uint8_t KEY_IDX_TO_LED_IDX_MAP_LEFT[CONFIG_KB_KEY_COUNT] = {        \
        __VA_ARGS__}
#define KEY_IDX_TO_LED_IDX_MAP_DEFINE_RIGHT(...)
#define LEDS_POSITIONS LEDS_POSITIONS_LEFT
#define KEY_IDX_TO_LED_IDX_MAP KEY_IDX_TO_LED_IDX_MAP_LEFT
#endif // CONFIG_YKB_LEFT

#define LEDS_POSITIONS_DEFINE_BOTH(...)                                        \
    LEDS_POSITIONS_LEFT(__VA_ARGS__);                                          \
    LEDS_POSITIONS_RIGHT(__VA_ARGS__)

#define KEY_IDX_TO_LED_IDX_MAP_DEFINE_BOTH(...)                                \
    KEY_IDX_TO_LED_IDX_MAP_DEFINE_LEFT(__VA_ARGS__);                           \
    KEY_IDX_TO_LED_IDX_MAP_DEFINE_RIGHT(__VA_ARGS__)

#endif // KB_LEDS_GEOM_H
