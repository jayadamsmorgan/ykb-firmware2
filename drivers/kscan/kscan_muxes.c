// SPDX-License-Identifier: Apache-2.0
#define DT_DRV_COMPAT kscan_muxes

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/devicetree/io-channels.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/kscan.h>
#include <drivers/mux.h>

LOG_MODULE_REGISTER(kscan_muxes, CONFIG_KSCAN_LOG_LEVEL);

struct kscan_muxes_config {

    const struct device *muxes[CONFIG_KSCAN_MUXES_MAX_MUX_CNT];
    const uint8_t mux_cnt;

    const struct adc_dt_spec cmn[CONFIG_KSCAN_MUXES_MAX_MUX_CNT];
};

static uint16_t read_io_channel(const struct kscan_muxes_config *cfg,
                                const struct adc_dt_spec *spec) {
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

static bool kscan_key_pressed_by_threshold(const struct kscan_muxes_config *cfg,
                                           const struct adc_dt_spec *spec,
                                           uint16_t *value,
                                           uint16_t threshold) {
    uint16_t val = read_io_channel(cfg, spec);
    if (val < threshold) {
        return false;
    }
    if (val) {
        *value = val;
    }
    return true;
}

static int kscan_muxes_poll_normal(const struct device *dev,
                                   uint8_t *pressed_keys,
                                   uint16_t *thresholds) {
    const struct kscan_muxes_config *cfg = dev->config;
    int err, ch_cnt;
    uint8_t key_index = 0;
    uint8_t pressed_count = 0;

    for (uint8_t i = 0; i < cfg->mux_cnt; ++i) {
        const struct device *mux = cfg->muxes[i];
        const struct adc_dt_spec *spec = &cfg->cmn[i];
        ch_cnt = mux_get_channel_amount(mux);
        if (ch_cnt < 0) {
            LOG_ERR("Could not get channel amount for MUX '%s' (%d)", mux->name,
                    ch_cnt);
            return -1;
        }
        for (int j = 0; j < ch_cnt; ++j) {
            if (kscan_key_pressed_by_threshold(cfg, spec, NULL,
                                               thresholds[key_index])) {
                pressed_keys[pressed_count] = key_index;
                pressed_count++;
                if (pressed_count >= CONFIG_KSCAN_MAX_SIMULTANIOUS_KEYS) {
                    return pressed_count;
                }
            }
            err = mux_select_next(dev);
            if (err) {
                LOG_ERR("Could not select channel %d for MUX '%s' (%d)", j,
                        mux->name, err);
                return -2;
            }
            key_index++;
        }
    }

    return pressed_count;
}

static int kscan_muxes_poll_race(const struct device *dev,
                                 uint16_t *thresholds) {
    const struct kscan_muxes_config *cfg = dev->config;
    int err, ch_cnt;
    int max_val = 0;
    int pressed_index = -1;
    int index = 0;

    for (uint8_t i = 0; i < cfg->mux_cnt; ++i) {
        const struct device *mux = cfg->muxes[i];
        const struct adc_dt_spec *spec = &cfg->cmn[i];
        ch_cnt = mux_get_channel_amount(mux);
        if (ch_cnt < 0) {
            LOG_ERR("Could not get channel amount for MUX '%s' (%d)", mux->name,
                    ch_cnt);
            return -2;
        }
        for (int j = 0; j < ch_cnt; ++j) {
            uint16_t value;
            if (kscan_key_pressed_by_threshold(cfg, spec, &value,
                                               thresholds[index])) {
                if (max_val < value) {
                    max_val = value;
                    pressed_index = index;
                }
            }
            err = mux_select_next(dev);
            if (err) {
                LOG_ERR("Could not select channel %d for MUX '%s' (%d)", j,
                        mux->name, err);
                return -2;
            }
            index++;
        }
    }

    return pressed_index;
}

static DEVICE_API(kscan, kscan_muxes_api) = {
    .poll_normal = &kscan_muxes_poll_normal,
    .poll_race = &kscan_muxes_poll_race,
};

static int kscan_muxes_init(const struct device *dev) {
    const struct kscan_muxes_config *cfg = dev->config;
    int err;

    /* Ensure all referenced mux provider devices are ready */
    for (uint8_t i = 0; i < cfg->mux_cnt; ++i) {
        const struct device *mux = cfg->muxes[i];
        if (!device_is_ready(mux)) {
            LOG_ERR("MUX '%s' is not ready", mux->name);
            return -ENODEV;
        }
        err = mux_enable(mux);
        if (err) {
            LOG_ERR("Could not enable MUX '%s' (%d)", mux->name, err);
            return -ENODEV;
        }
        LOG_DBG("MUX '%s' is ready", mux->name);
    }
    for (uint8_t i = 0; i < cfg->mux_cnt; ++i) {
        const struct adc_dt_spec *adc_spec = &cfg->cmn[i];
        if (!adc_is_ready_dt(adc_spec)) {
            LOG_ERR("ADC device '%s' is not ready", adc_spec->dev->name);
            return -ENODEV;
        }
        int err = adc_channel_setup_dt(adc_spec);
        if (err < 0) {
            LOG_ERR("Could not setup ADC channel '%d' (%d)",
                    adc_spec->channel_id, err);
            return -ENODEV;
        }
        LOG_DBG("Successfully set up ADC channel %d", adc_spec->channel_id);
    }

    LOG_INF("KScan (MUXes) ready: %u MUXes", cfg->mux_cnt);

    return 0;
}

/* ==== lengths ==== */
#define KSCAN_MUXES_LEN(n) DT_PROP_LEN(n, muxes)
#define KSCAN_IOCH_LEN(n) DT_PROP_LEN(n, io_channels)

/* ==== element builders (idx-based) ==== */
#define GET_MUX_DEV(idx, node_id)                                              \
    DEVICE_DT_GET(DT_PHANDLE_BY_IDX(node_id, muxes, idx))

#define DT_SPEC_AND_COMMA(node_id, prop, idx)                                  \
    ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

/* ==== per-instance define ==== */
#define KSCAN_MUXES_DEFINE(inst)                                               \
    BUILD_ASSERT(KSCAN_MUXES_LEN(DT_DRV_INST(inst)) ==                         \
                     KSCAN_IOCH_LEN(DT_DRV_INST(inst)),                        \
                 "muxes and io-channels must have the same length");           \
    BUILD_ASSERT(KSCAN_MUXES_LEN(DT_DRV_INST(inst)) <=                         \
                     CONFIG_KSCAN_MUXES_MAX_MUX_CNT,                           \
                 "Increase CONFIG_KSCAN_MUXES_MAX_MUX_CNT");                   \
                                                                               \
    static const struct kscan_muxes_config kscan_muxes_config_##inst = {       \
        .mux_cnt = KSCAN_MUXES_LEN(DT_DRV_INST(inst)),                         \
        .muxes = {LISTIFY(KSCAN_MUXES_LEN(DT_DRV_INST(inst)), GET_MUX_DEV,     \
                          (, ), DT_DRV_INST(inst))},                           \
        .cmn = {DT_FOREACH_PROP_ELEM(DT_PATH(kscan), io_channels,              \
                                     DT_SPEC_AND_COMMA)},                      \
    };                                                                         \
                                                                               \
    DEVICE_DT_INST_DEFINE(                                                     \
        inst, kscan_muxes_init, NULL, NULL, &kscan_muxes_config_##inst,        \
        POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &kscan_muxes_api);

DT_INST_FOREACH_STATUS_OKAY(KSCAN_MUXES_DEFINE)
