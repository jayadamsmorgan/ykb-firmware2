// SPDX-License-Identifier: Apache-2.0

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/kscan.h>
#include <drivers/mux.h>

#include <zephyr/usb/class/usbd_hid.h>
#include <zephyr/usb/usbd.h>

#include <lib/usb_connect.h>

#include <app_version.h>

LOG_MODULE_REGISTER(main, CONFIG_YKB_FIRMWARE_LOG_LEVEL);

static uint8_t usb_report[USB_CONNECT_HID_REPORT_COUNT];

int main(void) {

    const struct device *mux1, *mux2, *mux3, *kscan;

    printk("Zephyr Example Application %s\n", APP_VERSION_STRING);

    mux1 = DEVICE_DT_GET(DT_NODELABEL(mux1));
    if (!device_is_ready(mux1)) {
        LOG_ERR("Mux not ready");
        return 0;
    }

    mux2 = DEVICE_DT_GET(DT_NODELABEL(mux2));
    if (!device_is_ready(mux2)) {
        LOG_ERR("Mux not ready");
        return 0;
    }

    mux3 = DEVICE_DT_GET(DT_NODELABEL(mux3));
    if (!device_is_ready(mux3)) {
        LOG_ERR("Mux not ready");
        return 0;
    }

    kscan = DEVICE_DT_GET(DT_NODELABEL(kscan));
    if (!device_is_ready(kscan)) {
        LOG_ERR("Kscan not ready");
        return 0;
    }
    printk("Kscan is ready!\n");

    int ret = usb_connect_init();
    if (ret) {
        LOG_ERR("USBConnect error: %d", ret);
        return 0;
    }
    printk("USBConnect is ready!\n");

    while (true) {
        if (usb_connect_is_ready()) {
            usb_connect_handle_wakeup();
            usb_report[2] = 0;
            usb_connect_send(usb_report);
            k_timeout_t o = {.ticks = 50000};
            k_sleep(o);
            usb_report[2] = (uint8_t)4;
            usb_connect_send(usb_report);
        }
    }

    return 0;
}
