// SPDX-License-Identifier: Apache-2.0

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/kscan.h>
#include <drivers/mux.h>

#include <lib/connect/bt_connect.h>
#include <lib/connect/usb_connect.h>

#include <lib/keyboard/kb_handle.h>
#include <lib/keyboard/kb_settings.h>

#include <app_version.h>

LOG_MODULE_REGISTER(main, CONFIG_YKB_FIRMWARE_LOG_LEVEL);

#define KB_THREAD_STACK_SIZE 4096
#define KB_THREAD_PRIO K_PRIO_PREEMPT(2)

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
    printk("Kscan is ready!\n");

    int ret;

#if CONFIG_LIB_USB_CONNECT
    ret = usb_connect_init();
    if (ret) {
        LOG_ERR("USBConnect init error: %d", ret);
        return 0;
    }
    printk("USBConnect is ready!\n");
#endif // CONFIG_LIB_USB_CONNECT

#if CONFIG_LIB_BT_CONNECT
    ret = bt_connect_init();
    if (ret) {
        LOG_ERR("BTConnect init error: %d", ret);
        return 0;
    }
    printk("BTConnect is ready!\n");
#endif // CONFIG_LIB_BT_CONNECT

    ret = kb_settings_init();
    if (ret) {
        LOG_ERR("KBSettings init error: %d", ret);
        return 0;
    }
    printk("KBSettings is ready!\n");

    k_thread_create(&kb_thread_data, kb_thread_stack, KB_THREAD_STACK_SIZE,
                    kb_thread, NULL, NULL, NULL, KB_THREAD_PRIO, 0, K_NO_WAIT);
    k_thread_name_set(&kb_thread_data, "kb_thread");

    while (true) {
        k_sleep(K_SECONDS(1));
    }

    return 0;
}
