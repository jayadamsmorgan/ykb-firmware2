#ifndef CHOCO_V1_KEYSTROKES_H
#define CHOCO_V1_KEYSTROKES_H

#include <lib/keyboard/kb_fn_keystroke.h>

// Bluetooth
//

KB_FN_KEYSTROKE_DEFINE_LIB_BT(bt_fr_left, bt_connect_factory_reset, 17); // A
KB_FN_KEYSTROKE_DEFINE_LIB_BT(bt_sta_left, bt_connect_start_advertising,
                              14); // S
KB_FN_KEYSTROKE_DEFINE_LIB_BT(bt_fr_right, bt_connect_factory_reset, KEY_H);
KB_FN_KEYSTROKE_DEFINE_LIB_BT(bt_sta_right, bt_connect_start_advertising,
                              KEY_J);

// Backlight
//
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_n_left, kb_backlight_next_mode, KEY_B);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_p_left, kb_backlight_prev_mode, KEY_V);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_sb_min_left,
                                 kb_backlight_set_brightness_min, KEY_Q);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_sb_low_left,
                                 kb_backlight_set_brightness_low, KEY_W);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_sb_mid_left,
                                 kb_backlight_set_brightness_mid, KEY_E);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_sb_high_left,
                                 kb_backlight_set_brightness_high, KEY_R);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_sb_max_left,
                                 kb_backlight_set_brightness_max, KEY_T);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_toggle_left, kb_backlight_toggle, KEY_Z);

KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_n_right, kb_backlight_next_mode, KEY_M);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_p_right, kb_backlight_prev_mode, KEY_N);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_sb_min_right,
                                 kb_backlight_set_brightness_min, KEY_Y);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_sb_low_right,
                                 kb_backlight_set_brightness_low, KEY_U);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_sb_mid_right,
                                 kb_backlight_set_brightness_mid, KEY_I);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_sb_high_right,
                                 kb_backlight_set_brightness_high, KEY_O);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_sb_max_right,
                                 kb_backlight_set_brightness_max, KEY_P);
KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(bl_toggle_right, kb_backlight_toggle, KEY_L);

#endif // CHOCO_V1_KEYSTROKES_H
