// SPDX-License-Identifier: Apache-2.0
#define DT_DRV_COMPAT kscan_enables

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/devicetree/gpio.h>
#include <zephyr/devicetree/io-channels.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/kscan.h>
#include <drivers/mux.h>

LOG_MODULE_REGISTER(kscan_enables, CONFIG_KSCAN_LOG_LEVEL);

struct kscan_enables_config {
    const struct gpio_dt_spec gpios[CONFIG_KSCAN_ENABLES_MAX_EN_CNT];
    const uint8_t gpios_cnt;

    const struct adc_dt_spec cmn[CONFIG_KSCAN_ENABLES_MAX_ADC_CH_CNT];
    const uint8_t cmn_cnt;
    const uint8_t map[CONFIG_KSCAN_ENABLES_MAX_ADC_CH_CNT];
};

static uint16_t read_io_channel(const struct adc_dt_spec *spec) {
    uint16_t val = 0;
    struct adc_sequence sequence = {
        .buffer = &val,
        .buffer_size = sizeof(uint16_t),
    };
    adc_sequence_init_dt(spec, &sequence);
    int err = adc_read_dt(spec, &sequence);
    if (err < 0) {
        LOG_ERR("Could not read ADC channel '%d' (%d)", spec->channel_id, err);
        return 0;
    }
    return val;
}

static bool kscan_key_pressed_by_threshold(const struct adc_dt_spec *spec,
                                           uint16_t *value,
                                           uint16_t threshold) {
    uint16_t val = read_io_channel(spec);
    if (val < threshold) {
        return false;
    }
    if (value) {
        *value = val;
    }
    return true;
}

static inline void bm_set(uint32_t *bm, uint16_t idx) {
    bm[idx / 32] |= (1u << (idx % 32));
}

static int kscan_enables_poll_normal(const struct device *dev, uint32_t *bitmap,
                                     uint16_t *thresholds, uint16_t *values) {
    const struct kscan_enables_config *cfg = dev->config;
    int err;
    uint8_t key_index = 0;
    uint8_t pressed_count = 0;

    for (uint8_t i = 0; i < cfg->cmn_cnt; ++i) {
        const struct adc_dt_spec *adc_spec = &cfg->cmn[i];
        for (uint8_t j = 0; j < cfg->map[i]; ++j) {
            err = gpio_pin_set_dt(&cfg->gpios[key_index], 1);
            if (err)
                goto gpio_set_err;

            k_sleep(K_MSEC(1));

            uint16_t val;
            if (kscan_key_pressed_by_threshold(adc_spec, &val,
                                               thresholds[key_index])) {
                bm_set(bitmap, key_index);
                pressed_count++;
            }
            if (values) {
                values[key_index] = val;
            }

            err = gpio_pin_set_dt(&cfg->gpios[key_index], 0);
            if (err)
                goto gpio_set_err;

            key_index++;
        }
    }

    return pressed_count;

gpio_set_err:
    LOG_ERR("Unable to set gpio pin with index %d (err %d)", key_index, err);
    return -1;
}

static int kscan_enables_poll_race(const struct device *dev, uint32_t *bitmap,
                                   uint16_t *thresholds, uint16_t *values) {
    const struct kscan_enables_config *cfg = dev->config;
    int err;
    int max_val = 0;
    int pressed_index = -1;
    int key_index = 0;

    for (uint8_t i = 0; i < cfg->cmn_cnt; ++i) {
        const struct adc_dt_spec *spec = &cfg->cmn[i];
        for (uint8_t j = 0; j < cfg->map[i]; ++j) {
            err = gpio_pin_set_dt(&cfg->gpios[key_index], 1);
            if (err)
                goto gpio_set_err;

            uint16_t value;
            if (kscan_key_pressed_by_threshold(spec, &value,
                                               thresholds[key_index])) {
                if (max_val < value) {
                    max_val = value;
                    pressed_index = key_index;
                }
            }
            if (values) {
                values[key_index] = value;
            }

            err = gpio_pin_set_dt(&cfg->gpios[key_index], 0);
            if (err)
                goto gpio_set_err;

            key_index++;
        }
    }

    if (pressed_index >= 0) {
        bm_set(bitmap, key_index);
    }

    return pressed_index;

gpio_set_err:
    LOG_ERR("Unable to set gpio pin with index %d (err %d)", key_index, err);
    return -2;
}

static DEVICE_API(kscan, kscan_enables_api) = {
    .poll_normal = &kscan_enables_poll_normal,
    .poll_race = &kscan_enables_poll_race,
};

static int kscan_enables_init(const struct device *dev) {
    const struct kscan_enables_config *cfg = dev->config;

    for (uint8_t i = 0; i < cfg->gpios_cnt; ++i) {
        const struct gpio_dt_spec *pin = &cfg->gpios[i];
        if (!device_is_ready(pin->port))
            return -ENODEV;
        int ret = gpio_pin_configure_dt(pin, GPIO_OUTPUT_INACTIVE);
        if (ret)
            return ret;
    }
    LOG_DBG("GPIO pins are set up");

    for (uint8_t i = 0; i < cfg->cmn_cnt; ++i) {
        const struct adc_dt_spec *adc = &cfg->cmn[i];
        if (!adc_is_ready_dt(adc)) {
            LOG_ERR("ADC device '%s' is not ready", adc->dev->name);
            return -ENODEV;
        }
        int err = adc_channel_setup_dt(adc);
        if (err < 0) {
            LOG_ERR("Could not setup ADC channel '%d' (%d)", adc->channel_id,
                    err);
            return -ENODEV;
        }
        LOG_DBG("Successfully set up ADC channel %d", adc->channel_id);
    }

    return 0;
}

#define DT_GPIO_SPEC_AND_COMMA(node_id, prop, idx)                             \
    GPIO_DT_SPEC_GET_BY_IDX(node_id, prop, idx),

#define DT_ADC_SPEC_AND_COMMA(node_id, prop, idx)                              \
    ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

#define ADD_PROP_ELEM(node_id, prop, idx) +DT_PROP_BY_IDX(node_id, prop, idx)

#define KSCAN_ENABLES_DEFINE(inst)                                             \
    BUILD_ASSERT(DT_INST_PROP_LEN(inst, io_channels) ==                        \
                     DT_INST_PROP_LEN(inst, map),                              \
                 "io-channels length should be equal to map length");          \
    BUILD_ASSERT(                                                              \
        DT_INST_PROP_LEN(inst, gpios) ==                                       \
            (0 DT_FOREACH_PROP_ELEM(DT_DRV_INST(inst), map, ADD_PROP_ELEM)),   \
        "sum(map[]) must equal to gpios length");                              \
                                                                               \
    static const struct kscan_enables_config kscan_enables_config_##inst = {   \
        .gpios_cnt = DT_INST_PROP_LEN(inst, gpios),                            \
        .gpios = {DT_FOREACH_PROP_ELEM(DT_DRV_INST(inst), gpios,               \
                                       DT_GPIO_SPEC_AND_COMMA)},               \
        .cmn_cnt = DT_INST_PROP_LEN(inst, io_channels),                        \
        .cmn = {DT_FOREACH_PROP_ELEM(DT_DRV_INST(inst), io_channels,           \
                                     DT_ADC_SPEC_AND_COMMA)},                  \
        .map = DT_INST_PROP(inst, map),                                        \
    };                                                                         \
                                                                               \
    DEVICE_DT_INST_DEFINE(                                                     \
        inst, kscan_enables_init, NULL, NULL, &kscan_enables_config_##inst,    \
        POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &kscan_enables_api);

DT_INST_FOREACH_STATUS_OKAY(KSCAN_ENABLES_DEFINE)
