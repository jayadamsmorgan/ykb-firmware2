#ifndef KB_FN_KEYSTROKE_H_
#define KB_FN_KEYSTROKE_H_

#include <zephyr/sys/iterable_sections.h>

#include <lib/keyboard/kb_keys.h>

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

#define __KB_FN_KEYSTROKE_DEFINE(ks_name, callback, /* keys... */...)          \
    enum { __kb_cnt_##ks_name = __KB_COUNT_(__VA_ARGS__) };                    \
    BUILD_ASSERT(__kb_cnt_##ks_name > 0,                                       \
                 "FN keystroke key count should be greater than 0");           \
    BUILD_ASSERT(__kb_cnt_##ks_name <= CONFIG_KB_FN_KEYSTROKE_MAX_KEYS,        \
                 "FN keystroke key count exceeds limit " STRINGIFY(            \
                     CONFIG_KB_FN_KEYSTROKE_MAX_KEYS));                        \
    static STRUCT_SECTION_ITERABLE(kb_fn_keystroke,                            \
                                   __kb_fn_keystroke_##ks_name) = {            \
        .name = STRINGIFY(ks_name),                                            \
        .count = __kb_cnt_##ks_name,                                           \
        .cb = callback,                                                        \
        .keys = {__VA_ARGS__},                                                 \
    }

#if CONFIG_LIB_BT_CONNECT
#define KB_FN_KEYSTROKE_DEFINE_LIB_BT(name, callback, /* keys... */...)        \
    __KB_FN_KEYSTROKE_DEFINE(name, callback, __VA_ARGS__)
#else
#define KB_FN_KEYSTROKE_DEFINE_LIB_BT(name, callback, /* keys... */...)
#endif // CONFIG_LIB_BT_CONNECT

#if CONFIG_LIB_USB_CONNECT
#define KB_FN_KEYSTROKE_DEFINE_LIB_USB(name, callback, /* keys... */...)       \
    __KB_FN_KEYSTROKE_DEFINE(name, callback, __VA_ARGS__)
#else
#define KB_FN_KEYSTROKE_DEFINE_LIB_USB(callback, /* keys... */...)
#endif // CONFIG_LIB_USB_CONNECT

#define KB_FN_KEYSTROKE_DEFINE_LIB_KB_SETTINGS(name, callback,                 \
                                               /* keys... */...)               \
    __KB_FN_KEYSTROKE_DEFINE(name, callback, __VA_ARGS__)

#if CONFIG_KB_BACKLIGHT
#define KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(name, callback, /* keys... */...)     \
    __KB_FN_KEYSTROKE_DEFINE(name, callback, __VA_ARGS__)
#else
#define KB_FN_KEYSTROKE_DEFINE_LIB_KB_BL(name, callback, /* keys... */...)
#endif // CONFIG_KB_BACKLIGHT

#endif // KB_FN_KEYSTROKE_H_
