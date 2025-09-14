// SPDX-License-Identifier: Apache-2.0

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/kscan.h>
#include <drivers/mux.h>

#include <app_version.h>

LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

#define BLINK_PERIOD_MS_STEP 100U
#define BLINK_PERIOD_MS_MAX 1000U

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

    return 0;
}
