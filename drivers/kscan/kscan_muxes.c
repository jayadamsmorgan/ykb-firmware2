// SPDX-License-Identifier: Apache-2.0
#define DT_DRV_COMPAT kscan_muxes

#include <zephyr/device.h>

#include <zephyr/devicetree.h>
#include <zephyr/devicetree/io-channels.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <app/drivers/kscan.h>
#include <app/drivers/mux.h>

LOG_MODULE_REGISTER(kscan_muxes, CONFIG_KSCAN_LOG_LEVEL);

struct kscan_muxes_config {
    const struct device *muxes[CONFIG_KSCAN_MUXES_MAX_MUX_CNT];
    uint8_t mux_cnt;
    const struct device *cmn[CONFIG_KSCAN_MUXES_MAX_MUX_CNT];
    uint8_t cmn_cnt;
};

struct kscan_muxes_data {
    int smth;
};

static int kscan_muxes_poll_normal(const struct device *dev,
                                   uint8_t *pressed_keys,
                                   size_t max_pressed_keys) {
    return 0;
}

static int kscan_muxes_poll_race(const struct device *dev) {
    return 0;
}

static DEVICE_API(kscan, kscan_muxes_api) = {
    .poll_normal = &kscan_muxes_poll_normal,
    .poll_race = &kscan_muxes_poll_race,
};

static int kscan_muxes_init(const struct device *dev) {
    const struct kscan_muxes_config *cfg = dev->config;

    /* Ensure all referenced mux provider devices are ready */
    for (int i = 0; i < cfg->mux_cnt; ++i) {
        if (!device_is_ready(cfg->muxes[i])) {
            LOG_ERR("mux dev %d not ready", i);
            return -ENODEV;
        }
    }
    for (int i = 0; i < cfg->cmn_cnt; ++i) {
        if (!device_is_ready(cfg->cmn[i])) {
            LOG_ERR("adc dev %d not ready", i);
            return -ENODEV;
        }
    }

    LOG_INF("kscan-muxes ready: %u mux channels", cfg->mux_cnt);
    return 0;
}

/* ==== lengths ==== */
#define KSCAN_MUXES_LEN(n) DT_PROP_LEN(n, muxes)
#define KSCAN_IOCH_LEN(n) DT_PROP_LEN(n, io_channels)

/* ==== element builders (idx-based) ==== */
#define GET_MUX_DEV(idx, node_id)                                              \
    DEVICE_DT_GET(DT_PHANDLE_BY_IDX(node_id, muxes, idx))

#define GET_ADC_DEV(idx, node_id)                                              \
    DEVICE_DT_GET(DT_IO_CHANNELS_CTLR_BY_IDX(node_id, idx))

/* ==== per-instance define ==== */
#define KSCAN_MUXES_DEFINE(inst)                                               \
    BUILD_ASSERT(KSCAN_MUXES_LEN(DT_DRV_INST(inst)) ==                         \
                     KSCAN_IOCH_LEN(DT_DRV_INST(inst)),                        \
                 "muxes and io-channels must have the same length");           \
    BUILD_ASSERT(KSCAN_MUXES_LEN(DT_DRV_INST(inst)) <=                         \
                     CONFIG_KSCAN_MUXES_MAX_MUX_CNT,                           \
                 "Increase CONFIG_KSCAN_MUXES_MAX_MUX_CNT");                   \
                                                                               \
    static struct kscan_muxes_data kscan_muxes_data_##inst;                    \
                                                                               \
    static const struct kscan_muxes_config kscan_muxes_config_##inst = {       \
        .mux_cnt = KSCAN_MUXES_LEN(DT_DRV_INST(inst)),                         \
        .cmn_cnt = KSCAN_IOCH_LEN(DT_DRV_INST(inst)),                          \
        .muxes = {LISTIFY(KSCAN_MUXES_LEN(DT_DRV_INST(inst)), GET_MUX_DEV,     \
                          (, ), DT_DRV_INST(inst))},                           \
        .cmn = {LISTIFY(KSCAN_IOCH_LEN(DT_DRV_INST(inst)), GET_ADC_DEV, (, ),  \
                        DT_DRV_INST(inst))},                                   \
    };                                                                         \
                                                                               \
    DEVICE_DT_INST_DEFINE(                                                     \
        inst, kscan_muxes_init, NULL, &kscan_muxes_data_##inst,                \
        &kscan_muxes_config_##inst, POST_KERNEL,                               \
        CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &kscan_muxes_api);

DT_INST_FOREACH_STATUS_OKAY(KSCAN_MUXES_DEFINE)
