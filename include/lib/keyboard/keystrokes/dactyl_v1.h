#ifndef DACTYL_V1_KEYSTROKES_H
#define DACTYL_V1_KEYSTROKES_H

#include <lib/keyboard/kb_fn_keystroke.h>

// Bluetooth
//
KB_FN_KEYSTROKE_DEFINE_LIB_BT(bt_cfr_left, bt_connect_factory_reset, KEY_R);
KB_FN_KEYSTROKE_DEFINE_LIB_BT(bt_sa_left, bt_connect_start_advertising, KEY_A);
KB_FN_KEYSTROKE_DEFINE_LIB_BT(bt_cfr_right, bt_connect_factory_reset,
                              KEY_BACKSLASH_VERTICALBAR);
KB_FN_KEYSTROKE_DEFINE_LIB_BT(bt_sa_right, bt_connect_start_advertising, KEY_P);

// Backlight
//
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_n_left, kb_backlight_next_mode, KEY_B);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_p_left, kb_backlight_prev_mode, KEY_V);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_br_min_left,
                                 kb_backlight_set_brightness_min,
                                 KEY_1_EXCLAMATION);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_br_low_left,
                                 kb_backlight_set_brightness_low, KEY_2_ATSIGN);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_br_mid_left,
                                 kb_backlight_set_brightness_mid,
                                 KEY_3_NUMBERSIGN);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_br_high_left,
                                 kb_backlight_set_brightness_high,
                                 KEY_4_DOLLARSIGN);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_br_max_left,
                                 kb_backlight_set_brightness_max,
                                 KEY_5_PERCENTSIGN);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_toggle_left, kb_backlight_toggle, KEY_Z);

KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_n_right, kb_backlight_next_mode, KEY_N);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_p_right, kb_backlight_prev_mode, KEY_M);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_br_min_right,
                                 kb_backlight_set_brightness_min, KEY_6_CARET);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_br_low_right,
                                 kb_backlight_set_brightness_low,
                                 KEY_7_AMPERSAND);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_br_mid_right,
                                 kb_backlight_set_brightness_mid, KEY_8_STAR);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_br_high_right,
                                 kb_backlight_set_brightness_high,
                                 KEY_9_LPAREN);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_br_max_right,
                                 kb_backlight_set_brightness_max, KEY_0_RPAREN);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_toggle_right, kb_backlight_toggle,
                                 KEY_SLASH_QUESTIONMARK);

#endif // DACTYL_V1_KEYSTROKES_H
