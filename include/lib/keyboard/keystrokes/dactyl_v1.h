#ifndef DACTYL_V1_KEYSTROKES_H
#define DACTYL_V1_KEYSTROKES_H

#include <lib/keyboard/kb_fn_keystroke.h>

// Bluetooth
//
KB_FN_KEYSTROKE_DEFINE_LIB_BT(bt_connect_factory_reset, KEY_R);
KB_FN_KEYSTROKE_DEFINE_LIB_BT(bt_connect_start_advertising, KEY_P);

// Backlight
//
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(kb_backlight_next_mode, KEY_N);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(kb_backlight_prev_mode, KEY_B);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(kb_backlight_set_brightness_min,
                                 KEY_1_EXCLAMATION);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(kb_backlight_set_brightness_low, KEY_2_ATSIGN);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(kb_backlight_set_brightness_mid,
                                 KEY_3_NUMBERSIGN);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(kb_backlight_set_brightness_high,
                                 KEY_4_DOLLARSIGN);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(kb_backlight_set_brightness_max,
                                 KEY_5_PERCENTSIGN);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(kb_backlight_toggle, KEY_Z);

#endif // DACTYL_V1_KEYSTROKES_H
