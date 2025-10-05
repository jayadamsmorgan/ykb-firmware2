#ifndef KB_LEDS_GEOM_CHOCO_V1_H
#define KB_LEDS_GEOM_CHOCO_V1_H

#include <lib/led/kb_leds_geom.h>

LEDS_POSITIONS_DEFINE_LEFT(POS(9.5, 3.5), POS(9.5, 1.7), POS(9.5, -0.2),
                           POS(7.6, -0.5), POS(7.6, 1.5), POS(7.6, 3.3),
                           POS(12.8, 6.7), POS(11.1, 5.9), POS(8.7, 5.7),
                           POS(6.6, 5.5), POS(5.7, 3.1), POS(5.7, 1.2),
                           POS(5.7, -0.7), POS(3.8, -0.4), POS(3.8, 1.4),
                           POS(3.8, 3.3), POS(1.9, 3.8), POS(1.9, 1.9),
                           POS(1.9, 0), POS(0, 0), POS(0, 1.9), POS(0, 3.8));

LEDS_POSITIONS_DEFINE_RIGHT(POS(-9.5, -3.5), POS(-9.5, -1.7), POS(-9.5, 0.2),
                            POS(-7.6, 0.5), POS(-7.6, -1.5), POS(-7.6, -3.3),
                            POS(-12.8, -6.7), POS(-11.1, -5.9), POS(-8.7, -5.7),
                            POS(-6.6, -5.5), POS(-5.7, -3.1), POS(-5.7, -1.2),
                            POS(-5.7, 0.7), POS(-3.8, 0.4), POS(-3.8, -1.4),
                            POS(-3.8, -3.3), POS(-1.9, -3.8), POS(-1.9, -1.9),
                            POS(-1.9, 0), POS(0, 0), POS(0, -1.9),
                            POS(0, -3.8));

KEY_IDX_TO_LED_IDX_MAP_DEFINE_BOTH(2, 1, 0, 6, 7, 8, 9, 3, 4, 5, 12, 11, 10, 13,
                                   14, 15, 18, 17, 16, 19, 20, 21);

#endif // KB_LEDS_GEOM_CHOCO_V1_H
