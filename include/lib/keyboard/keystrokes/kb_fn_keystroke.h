#ifndef LIB_BT_CONNECT_KB_FN_KEYSTROKE_H_
#define LIB_BT_CONNECT_KB_FN_KEYSTROKE_H_

#include <zephyr/sys/iterable_sections.h>

#include <lib/keyboard/kb_keys.h>
#include <lib/keyboard/keystrokes/kb_fn_keystroke.h>

#include <lib/connect/bt_connect.h>
#include <lib/connect/usb_connect.h>
#include <lib/keyboard/kb_settings.h>
#include <lib/led/kb_backlight.h>

struct kb_fn_keystroke {
    const char *name;
    const uint8_t count;
    void (*cb)(void);
    const uint8_t keys[CONFIG_KB_FN_KEYSTROKE_MAX_KEYS];
};

#define __KB_COUNT_(...) (sizeof((uint8_t[]){__VA_ARGS__}) / sizeof(uint8_t))

#define __KB_FN_KEYSTROKE_DEFINE(callback, /* keys... */...)                   \
    enum { __kb_cnt_##callback = __KB_COUNT_(__VA_ARGS__) };                   \
    BUILD_ASSERT(__kb_cnt_##callback > 0,                                      \
                 "FN keystroke key count should be greater than 0");           \
    BUILD_ASSERT(__kb_cnt_##callback <= CONFIG_KB_FN_KEYSTROKE_MAX_KEYS,       \
                 "FN keystroke key count exceeds limit " STRINGIFY(            \
                     CONFIG_KB_FN_KEYSTROKE_MAX_KEYS));                        \
    static STRUCT_SECTION_ITERABLE(kb_fn_keystroke,                            \
                                   __kb_fn_keystroke_##callback) = {           \
        .name = STRINGIFY(callback),                                           \
        .count = __kb_cnt_##callback,                                          \
        .cb = callback,                                                        \
        .keys = {__VA_ARGS__},                                                 \
    }

#if CONFIG_LIB_BT_CONNECT
#define KB_FN_KEYSTROKE_DEFINE_LIB_BT(callback, /* keys... */...)              \
    __KB_FN_KEYSTROKE_DEFINE(callback, __VA_ARGS__)
#else
#define KB_FN_KEYSTROKE_DEFINE_LIB_BT(callback, /* keys... */...)
#endif // CONFIG_LIB_BT_CONNECT

#if CONFIG_LIB_USB_CONNECT
#define KB_FN_KEYSTROKE_DEFINE_LIB_USB(callback, /* keys... */...)             \
    __KB_FN_KEYSTROKE_DEFINE(callback, __VA_ARGS__)
#else
#define KB_FN_KEYSTROKE_DEFINE_LIB_USB(callback, /* keys... */...)
#endif // CONFIG_LIB_USB_CONNECT

#define KB_FN_KEYSTROKE_DEFINE_LIB_KB_SETTINGS(callback, /* keys... */...)     \
    KB_FN_KEYSTROKE_DEFINE(callback, __VA_ARGS__)

#if CONFIG_KB_BACKLIGHT
#define KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(callback, /* keys... */...)           \
    KB_FN_KEYSTROKE_DEFINE(keystroke_name, keys_arr, key_count, callback)
#else
#define KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(callback, /* keys... */...)
#endif // CONFIG_KB_BACKLIGHT

#endif // LIB_BT_CONNECT_KB_FN_KEYSTROKE_H_
