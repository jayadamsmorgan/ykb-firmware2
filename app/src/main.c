// SPDX-License-Identifier: Apache-2.0

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/kscan.h>
#include <drivers/mux.h>
#include <zephyr/drivers/led_strip.h>

#include <lib/connect/bt_connect.h>
#include <lib/connect/usb_connect.h>

#include <lib/keyboard/kb_handle.h>
#include <lib/keyboard/kb_settings.h>

#include <app_version.h>

LOG_MODULE_REGISTER(main, CONFIG_YKB_FIRMWARE_LOG_LEVEL);

#define KB_THREAD_STACK_SIZE 4096
#define KB_THREAD_PRIO K_PRIO_PREEMPT(0)

static K_THREAD_STACK_DEFINE(kb_thread_stack, KB_THREAD_STACK_SIZE);
static struct k_thread kb_thread_data;

static void kb_thread(void *a, void *b, void *c) {
    while (true) {
        kb_handle();
    }
}

int main(void) {

    const struct device *kscan;

    kscan = DEVICE_DT_GET(DT_NODELABEL(kscan));
    if (!device_is_ready(kscan)) {
        LOG_ERR("Kscan not ready");
        return 0;
    }
    LOG_DBG("Kscan is ready!");

    int ret;

#if CONFIG_LIB_USB_CONNECT
    ret = usb_connect_init();
    if (ret) {
        LOG_ERR("USBConnect init error: %d", ret);
        return 0;
    }
    LOG_DBG("USBConnect is ready!");
#endif // CONFIG_LIB_USB_CONNECT

#if CONFIG_LIB_BT_CONNECT
    ret = bt_connect_init();
    if (ret) {
        LOG_ERR("BTConnect init error: %d", ret);
        return 0;
    }
    LOG_DBG("BTConnect is ready!");
#endif // CONFIG_LIB_BT_CONNECT

    ret = kb_settings_init();
    if (ret) {
        LOG_ERR("KBSettings init error: %d", ret);
        return 0;
    }
    LOG_DBG("KBSettings is ready!");

    const struct device *strip = DEVICE_DT_GET(DT_NODELABEL(led_strip));
    if (!strip || !device_is_ready(strip)) {
        LOG_ERR("Strip device not ready");
    }
    LOG_DBG("Strip ready");

    struct led_rgb pixels[] = {
        (struct led_rgb){.r = 0, .g = 100, .b = 0},
        (struct led_rgb){.r = 100, .g = 0, .b = 0},
        (struct led_rgb){.r = 0, .g = 0, .b = 100},
        (struct led_rgb){.r = 0, .g = 50, .b = 50},
        (struct led_rgb){.r = 50, .g = 50, .b = 0},
    };

    ret = led_strip_update_rgb(strip, pixels, 5);
    if (ret) {
        LOG_ERR("Strip update err %d", ret);
    }

    // k_thread_create(&kb_thread_data, kb_thread_stack, KB_THREAD_STACK_SIZE,
    //                 kb_thread, NULL, NULL, NULL, KB_THREAD_PRIO, 0,
    //                 K_NO_WAIT);
    // k_thread_name_set(&kb_thread_data, "kb_thread");

    // uint8_t buffer[8] = {0};
    // buffer[2] = 0x18;
    // while (true) {
    //     if (bt_connect_is_ready()) {
    //         bt_connect_send(buffer);
    //     }
    //     k_sleep(K_SECONDS(2));
    // }

    return 0;
}
