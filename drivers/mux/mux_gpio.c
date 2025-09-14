// SPDX-License-Identifier: Apache-2.0
#define DT_DRV_COMPAT mux_gpio

#include <zephyr/device.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <app/drivers/mux.h>

LOG_MODULE_REGISTER(mux_gpio, CONFIG_MUX_LOG_LEVEL);

struct mux_gpio_config {
    struct gpio_dt_spec
        sel[CONFIG_MUX_GPIO_MAX_SEL_CNT]; /* support up to 8 select lines */
    uint8_t sel_cnt;
    struct gpio_dt_spec en; /* optional */
    bool has_en;
    uint16_t channels; /* actual usable channels */
    uint32_t settle_us;
    /* Optional LUT: drive pattern for channel i */
    const uint32_t *lut;
    uint16_t lut_len;
};

struct mux_gpio_data {
    int current;       /* -1 unknown */
    int current_lut_i; /* -1 unknown */
    bool enabled;
};

static int drive_bits(const struct mux_gpio_config *cfg, uint32_t pattern) {
    for (uint8_t i = 0; i < cfg->sel_cnt; ++i) {
        int level = (pattern >> i) & 0x1;
        int ret = gpio_pin_set_dt(&cfg->sel[i], level);
        if (ret) {
            return ret;
        }
    }
    return 0;
}

static int mux_gpio_select_next(const struct device *dev) {
    const struct mux_gpio_config *cfg = dev->config;
    struct mux_gpio_data *data = dev->data;

    int cur_i = cfg->lut ? data->current_lut_i : data->current;
    int next = (cur_i + 1) % cfg->channels;
    next = cfg->lut ? cfg->lut[next] : next;

    if (cfg->has_en && !data->enabled) {
        int ret = gpio_pin_set_dt(&cfg->en, 1);
        if (ret) {
            return ret;
        }
        data->enabled = true;
    }

    int ret = drive_bits(cfg, next);
    if (ret) {
        return ret;
    }

    if (cfg->settle_us) {
        k_busy_wait(cfg->settle_us);
    }

    data->current = (int)next;
    if (cfg->lut)
        data->current_lut_i = (data->current_lut_i + 1) % cfg->lut_len;

    return 0;
}

static int mux_gpio_select(const struct device *dev, uint32_t channel) {
    const struct mux_gpio_config *cfg = dev->config;
    struct mux_gpio_data *data = dev->data;

    if (channel >= cfg->channels) {
        return -EINVAL;
    }

    /* pattern is either LUT[channel] or simply channel index */
    uint32_t pattern = cfg->lut ? cfg->lut[channel] : channel;

    if (cfg->has_en && !data->enabled) {
        int ret = gpio_pin_set_dt(&cfg->en, 1);
        if (ret) {
            return ret;
        }
        data->enabled = true;
    }

    int ret = drive_bits(cfg, pattern);
    if (ret) {
        return ret;
    }

    if (cfg->settle_us) {
        k_busy_wait(cfg->settle_us);
    }

    data->current = (int)channel;
    return 0;
}

static int mux_gpio_enable(const struct device *dev) {
    const struct mux_gpio_config *cfg = dev->config;

    if (!cfg->has_en) {
        return 0;
    }

    int ret = gpio_pin_set_dt(&cfg->en, 1);

    return ret;
}

static int mux_gpio_disable(const struct device *dev) {
    const struct mux_gpio_config *cfg = dev->config;

    if (!cfg->has_en) {
        return 0;
    }

    int ret = gpio_pin_set_dt(&cfg->en, 0);

    return ret;
}

static int mux_gpio_get_current_channel(const struct device *dev) {
    const struct mux_gpio_data *data = dev->data;
    return data->current;
}

static int mux_gpio_get_channel_amount(const struct device *dev) {
    const struct mux_gpio_config *cfg = dev->config;
    return cfg->channels;
}

static int mux_gpio_is_enabled(const struct device *dev) {
    const struct mux_gpio_data *data = dev->data;
    return data->enabled;
}

static DEVICE_API(mux, mux_gpio_api) = {
    .select = &mux_gpio_select,
    .select_next = &mux_gpio_select_next,

    .enable = &mux_gpio_enable,
    .disable = &mux_gpio_disable,
    .is_enabled = &mux_gpio_is_enabled,

    .get_current_channel = &mux_gpio_get_current_channel,
    .get_channel_amount = &mux_gpio_get_channel_amount,
};

static int mux_gpio_init(const struct device *dev) {
    const struct mux_gpio_config *cfg = dev->config;
    struct mux_gpio_data *data = dev->data;

    /* Configure select pins */
    for (uint8_t i = 0; i < cfg->sel_cnt; ++i) {
        if (!device_is_ready(cfg->sel[i].port))
            return -ENODEV;
        int ret = gpio_pin_configure_dt(&cfg->sel[i], GPIO_OUTPUT_INACTIVE);
        if (ret)
            return ret;
    }

    /* Configure enable pin */
    if (cfg->has_en) {
        if (!device_is_ready(cfg->en.port))
            return -ENODEV;
        int ret = gpio_pin_configure_dt(&cfg->en, GPIO_OUTPUT_INACTIVE);
        if (ret)
            return ret;
        data->enabled = false;
    } else {
        data->enabled = true;
    }

    data->current = -1;

    return 0;
}

#define GET_SEL_SPEC(idx, node_id)                                             \
    GPIO_DT_SPEC_GET_BY_IDX(node_id, sel_gpios, idx)

#define LUT_OR_NULL(node_id)                                                   \
    COND_CODE_1(DT_NODE_HAS_PROP(node_id, channel_map),                        \
                (DT_PROP(node_id, channel_map)), (NULL))

#define LUT_LEN(node_id)                                                       \
    COND_CODE_1(DT_NODE_HAS_PROP(node_id, channel_map),                        \
                (DT_PROP_LEN(node_id, channel_map)), (0))

#define CHANNELS_VAL(node_id)                                                  \
    COND_CODE_1(DT_NODE_HAS_PROP(node_id, channel_map),                        \
                (DT_PROP_LEN(node_id, channel_map)),                           \
                (COND_CODE_1(DT_NODE_HAS_PROP(node_id, channels),              \
                             (DT_PROP(node_id, channels)),                     \
                             (1 << DT_PROP_LEN(node_id, sel_gpios)))))

#define MUX_GPIO_DEFINE(inst)                                                  \
    static struct mux_gpio_data data##inst;                                    \
                                                                               \
    static const struct mux_gpio_config config##inst = {                       \
        .sel = {LISTIFY(DT_PROP_LEN(DT_DRV_INST(inst), sel_gpios),             \
                        GET_SEL_SPEC, (, ), DT_DRV_INST(inst))},               \
        .sel_cnt = DT_PROP_LEN(DT_DRV_INST(inst), sel_gpios),                  \
        .en = GPIO_DT_SPEC_GET_OR(DT_DRV_INST(inst), enable_gpios, {0}),       \
        .has_en = DT_NODE_HAS_PROP(DT_DRV_INST(inst), enable_gpios),           \
        .channels = CHANNELS_VAL(DT_DRV_INST(inst)),                           \
        .settle_us = DT_PROP_OR(DT_DRV_INST(inst), settle_us, 0),              \
        .lut = LUT_OR_NULL(DT_DRV_INST(inst)),                                 \
        .lut_len = LUT_LEN(DT_DRV_INST(inst)),                                 \
    };                                                                         \
                                                                               \
    DEVICE_DT_INST_DEFINE(inst, mux_gpio_init, NULL, &data##inst,              \
                          &config##inst, POST_KERNEL,                          \
                          CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &mux_gpio_api);

DT_INST_FOREACH_STATUS_OKAY(MUX_GPIO_DEFINE)
