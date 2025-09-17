// SPDX-License-Identifier: Apache-2.0

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/kscan.h>
#include <drivers/mux.h>

#include <lib/connect/bt_connect.h>
#include <lib/connect/usb_connect.h>

#include <app_version.h>

LOG_MODULE_REGISTER(main, CONFIG_YKB_FIRMWARE_LOG_LEVEL);

// static uint8_t usb_report[USB_CONNECT_HID_REPORT_COUNT];

int main(void) {

    const struct device *kscan;

    printk("Zephyr Example Application %s\n", APP_VERSION_STRING);

    kscan = DEVICE_DT_GET(DT_NODELABEL(kscan));
    if (!device_is_ready(kscan)) {
        LOG_ERR("Kscan not ready");
        return 0;
    }
    printk("Kscan is ready!\n");

    int ret;

    ret = usb_connect_init();
    if (ret) {
        LOG_ERR("USBConnect init error: %d", ret);
        return 0;
    }
    printk("USBConnect is ready!\n");

    ret = bt_connect_init();
    if (ret) {
        LOG_ERR("BTConnect init error: %d", ret);
        return 0;
    }
    printk("BTConnect is ready!\n");

    return 0;
}
