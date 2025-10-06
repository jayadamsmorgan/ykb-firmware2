#include <lib/led/kb_bl_mode.h>
#include YKB_LEDS_GEOM_PATH
#include <string.h> // memset

// ---- Tunables --------------------------------------------------------------
#define MAX_SPOTS 10             // how many simultaneous "bruises"
#define BASE_LIFE_MS 900         // how long a bruise lasts
#define RADIUS_UNITS 6           // radius in your POS() units (e.g., mm)
#define RADIUS_Q Q(RADIUS_UNITS) // Q8.8 radius
// ---------------------------------------------------------------------------

typedef struct {
    bool alive;
    kb_fp16 cx, cy;   // center (Q8.8)
    uint32_t age_ms;  // age of bruise
    uint32_t life_ms; // lifespan
} spot_t;

typedef struct {
    size_t len; // led count (from init)
    spot_t spots[MAX_SPOTS];
} bruise_state_t;

static bruise_state_t S;

// pick side (LEFT by default if no side flag defined)
#if defined(CONFIG_KB_IS_LEFT) || defined(CONFIG_YKB_LEFT) ||                  \
    defined(KB_SIDE_LEFT)
#define LED_POS LEDS_POSITIONS_LEFT
#define KEY2LED KEY_IDX_TO_LED_IDX_MAP_LEFT
#else
#define LED_POS LEDS_POSITIONS_RIGHT
#define KEY2LED KEY_IDX_TO_LED_IDX_MAP_RIGHT
#endif

// distance^2 in Q16.16 (from kb_leds_geom.h helper)
static inline int32_t d2_led(uint8_t led, kb_fp16 cx, kb_fp16 cy) {
    return kb_leds_geom_sqdist_fp(LED_POS[led].x, LED_POS[led].y, cx, cy);
}

// Add/replace a spot at (cx,cy)
static void spawn_spot(kb_fp16 cx, kb_fp16 cy) {
    // find a free slot, else overwrite the oldest
    int victim = -1;
    uint32_t oldest = 0;
    for (int i = 0; i < MAX_SPOTS; ++i) {
        if (!S.spots[i].alive) {
            victim = i;
            break;
        }
        if (S.spots[i].age_ms >= oldest) {
            oldest = S.spots[i].age_ms;
            victim = i;
        }
    }
    spot_t *sp = &S.spots[victim];
    sp->alive = true;
    sp->cx = cx;
    sp->cy = cy;
    sp->age_ms = 0;
    sp->life_ms = BASE_LIFE_MS;
}

// on_event: light around the pressed key (release ignored)
static void on_event(kb_key_t *key) {
    if (!key || !key->pressed)
        return;

    // Map key index -> LED index on the current side
    uint8_t led_idx = KEY2LED[key->index];
    if (led_idx >= S.len)
        return;

    // Center at that LED's position
    spawn_spot(LED_POS[led_idx].x, LED_POS[led_idx].y);
}

// init/deinit
static void init(size_t len) {
    S.len = len;
    memset(S.spots, 0, sizeof(S.spots));
}

static void deinit(void) {
    memset(&S, 0, sizeof(S));
}

// Core apply: accumulate contributions from all alive spots
static void apply(uint32_t dt_ms, float speed, struct led_rgb *frame) {
    // advance ages (scale by speed; clamp to avoid wild jumps)
    if (dt_ms > 100)
        dt_ms = 100;
    uint32_t age_step = (uint32_t)((float)dt_ms * (speed > 0.f ? speed : 1.f));

    // precompute radius^2 in Q16.16
    const int32_t R2 = (int32_t)RADIUS_Q * (int32_t)RADIUS_Q; // Q16.16

    // clear frame
    for (size_t i = 0; i < S.len; ++i)
        frame[i] = (struct led_rgb){0};

    // accumulate red per LED from each spot
    for (int s = 0; s < MAX_SPOTS; ++s) {
        spot_t *sp = &S.spots[s];
        if (!sp->alive)
            continue;

        // envelope: linear fade (255 -> 0)
        uint32_t age = sp->age_ms;
        if (age >= sp->life_ms) {
            sp->alive = false;
            continue;
        }
        uint16_t env = 255u - (uint16_t)((age * 255u) / sp->life_ms);

        // each LED gets contribution if within radius
        for (size_t i = 0; i < S.len; ++i) {
            int32_t d2 = d2_led((uint8_t)i, sp->cx, sp->cy); // Q16.16
            if (d2 >= R2)
                continue; // outside circle

            // linear falloff with distance^2 (no sqrt):
            // val = env * (R2 - d2) / R2
            // Keep in 32-bit, clamp at 255
            uint32_t num = (uint32_t)(R2 - d2);
            uint32_t contrib = (env * num) / (uint32_t)R2; // 0..255
            uint16_t r = frame[i].r + (uint16_t)contrib;
            frame[i].r = (r > 255) ? 255 : (uint8_t)r;
        }

        sp->age_ms += age_step;
        if (sp->age_ms >= sp->life_ms)
            sp->alive = false;
    }

    // green/blue remain 0 (pure red)
}

KB_BL_MODE_DEFINE(press_bruise_red, init, deinit, apply, on_event);
