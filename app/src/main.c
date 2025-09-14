/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <app/drivers/blink.h>
#include <app/drivers/kscan.h>
#include <app/drivers/mux.h>

#include <app_version.h>

LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

#define BLINK_PERIOD_MS_STEP 100U
#define BLINK_PERIOD_MS_MAX 1000U

int main(void) {
    int ret;
    unsigned int period_ms = BLINK_PERIOD_MS_MAX;
    const struct device *sensor, *blink, *mux1, *mux2, *mux3, *kscan;
    struct sensor_value last_val = {0}, val;

    printk("Zephyr Example Application %s\n", APP_VERSION_STRING);

    sensor = DEVICE_DT_GET(DT_NODELABEL(example_sensor));
    if (!device_is_ready(sensor)) {
        LOG_ERR("Sensor not ready");
        return 0;
    }

    blink = DEVICE_DT_GET(DT_NODELABEL(blink_led));
    if (!device_is_ready(blink)) {
        LOG_ERR("Blink LED not ready");
        return 0;
    }

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

    ret = blink_off(blink);
    if (ret < 0) {
        LOG_ERR("Could not turn off LED (%d)", ret);
        return 0;
    }

    printk("Use the sensor to change LED blinking period\n");

    while (1) {
        ret = sensor_sample_fetch(sensor);
        if (ret < 0) {
            LOG_ERR("Could not fetch sample (%d)", ret);
            return 0;
        }

        ret = sensor_channel_get(sensor, SENSOR_CHAN_PROX, &val);
        if (ret < 0) {
            LOG_ERR("Could not get sample (%d)", ret);
            return 0;
        }

        if ((last_val.val1 == 0) && (val.val1 == 1)) {
            if (period_ms == 0U) {
                period_ms = BLINK_PERIOD_MS_MAX;
            } else {
                period_ms -= BLINK_PERIOD_MS_STEP;
            }

            printk("Proximity detected, setting LED period to %u ms\n",
                   period_ms);
            blink_set_period_ms(blink, period_ms);
        }

        last_val = val;

        k_sleep(K_MSEC(100));
    }

    return 0;
}
