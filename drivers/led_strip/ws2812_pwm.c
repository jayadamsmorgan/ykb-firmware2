/*
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT worldsemi_ws2812_pwm

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/devicetree/pwms.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/dt-bindings/led/led.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(ws2812_pwm, CONFIG_LED_STRIP_LOG_LEVEL);

/* WS2812 timing (@800 kHz) */
#define WS2812_BIT_NS 1250U
#define WS2812_T0H_NS 350U
#define WS2812_T1H_NS 700U
#define WS2812_RESET_US 80U

struct ws2812_pwm_cfg {
    struct pwm_dt_spec pwm;

    /* DMA from DT (controller dev, channel id, optional slot/request) */
    const struct device *dma_dev;
    uint32_t dma_channel;
    uint32_t dma_slot; /* set from DT; 0 if unused by your binding */

    /* Timer register addresses (DT-provided; vendor-specific as data) */
    uint32_t ccr_addr;  /* TIMx->CCRy */
    uint32_t dier_addr; /* TIMx->DIER */
    uint8_t dier_bit;   /* DIER bit to enable DMA request (CCyDE or UDE) */

    uint8_t num_colors;
    const uint8_t *color_mapping;
    size_t length;
};

struct ws2812_pwm_data {
    struct k_sem dma_done;
};

/* ---------------- DMA callback ---------------- */
static void ws2812_dma_cb(const struct device *dma_dev, void *user_data,
                          uint32_t channel, int status) {
    ARG_UNUSED(dma_dev);
    ARG_UNUSED(channel);
    ARG_UNUSED(status);
    struct ws2812_pwm_data *data = user_data;
    k_sem_give(&data->dma_done);
}

/* ---------------- Timing helpers --------------- */
static int ws2812_calc_cycles(const struct pwm_dt_spec *pwm,
                              uint32_t *period_cyc, uint32_t *duty0_cyc,
                              uint32_t *duty1_cyc) {
    uint64_t clk_hz = 0;
    int rc = pwm_get_cycles_per_sec(pwm->dev, pwm->channel, &clk_hz);
    if (rc) {
        return rc;
    }

    uint64_t period = (clk_hz * WS2812_BIT_NS) / 1000000000ULL;
    if (period == 0)
        period = 1;

    uint64_t d0 = (clk_hz * WS2812_T0H_NS) / 1000000000ULL;
    uint64_t d1 = (clk_hz * WS2812_T1H_NS) / 1000000000ULL;

    if (d0 == 0)
        d0 = 1;
    if (d1 == 0)
        d1 = 1;
    if (d0 >= period)
        d0 = period - 1;
    if (d1 >= period)
        d1 = period - 1;

    *period_cyc = (uint32_t)period;
    *duty0_cyc = (uint32_t)d0;
    *duty1_cyc = (uint32_t)d1;
    return 0;
}

/* ---------------- LED Strip API ---------------- */

static int ws2812_pwm_update_channels(const struct device *dev,
                                      uint8_t *channels, size_t num_channels) {
    const struct ws2812_pwm_cfg *cfg = dev->config;
    struct ws2812_pwm_data *data = dev->data;

    if (!channels || cfg->length == 0 || cfg->num_colors == 0) {
        return -EINVAL;
    }
    if (num_channels != cfg->length * cfg->num_colors) {
        LOG_ERR("%s: channels=%zu != length(%zu)*num_colors(%u)", dev->name,
                num_channels, cfg->length, cfg->num_colors);
        return -EINVAL;
    }

    /* Set PWM period once (duty 0) */
    uint32_t period_cyc, duty0_cyc, duty1_cyc;
    int rc = ws2812_calc_cycles(&cfg->pwm, &period_cyc, &duty0_cyc, &duty1_cyc);
    if (rc) {
        LOG_ERR("pwm timing calc failed: %d", rc);
        return rc;
    }
    rc = pwm_set_cycles(cfg->pwm.dev, cfg->pwm.channel, period_cyc, 0,
                        cfg->pwm.flags);
    if (rc) {
        LOG_ERR("pwm_set_cycles failed: %d", rc);
        return rc;
    }

    /* Build duty sequence on stack: one halfword per data bit */
    const size_t duty_len = num_channels * 8U;
    if (duty_len > 4096U) { /* keep stack reasonable; adjust if needed */
        LOG_ERR("frame too large for stack buffer: %zu bits", duty_len);
        return -E2BIG;
    }
    uint16_t duty[duty_len];
    size_t k = 0;
    for (size_t i = 0; i < num_channels; ++i) {
        uint8_t v = channels[i];
        for (int b = 7; b >= 0; --b) {
            duty[k++] =
                (v & BIT(b)) ? (uint16_t)duty1_cyc : (uint16_t)duty0_cyc;
        }
    }

    if (!device_is_ready(cfg->dma_dev)) {
        LOG_ERR("DMA device not ready");
        return -ENODEV;
    }

    /* Prepare DMA block: memory -> TIMx->CCRy, halfword */
    struct dma_block_config blk = {0};
    blk.block_size = (uint32_t)(duty_len * sizeof(uint16_t));
    blk.source_address = (uint32_t)(uintptr_t)duty;
    blk.dest_address = (uint32_t)cfg->ccr_addr;

    struct dma_config dcfg = {0};
    dcfg.user_data = data;
    dcfg.dma_callback = ws2812_dma_cb;
    dcfg.head_block = &blk;
    dcfg.channel_direction = MEMORY_TO_PERIPHERAL;

    dcfg.source_data_size = 2; /* halfword */
    dcfg.dest_data_size = 2;
    dcfg.source_burst_length = 1;
    dcfg.dest_burst_length = 1;
    dcfg.dma_slot = cfg->dma_slot; /* 0 if binding doesn’t use it */

    /* Enable timer’s DMA request (DIER.CCyDE or UDE). Address/bit from DT. */
    sys_set_bit((mem_addr_t)cfg->dier_addr, cfg->dier_bit);

    rc = dma_config(cfg->dma_dev, cfg->dma_channel, &dcfg);
    if (rc) {
        LOG_ERR("dma_config ch%u failed: %d", (unsigned)cfg->dma_channel, rc);
        sys_clear_bit((mem_addr_t)cfg->dier_addr, cfg->dier_bit);
        return rc;
    }

    k_sem_reset(&data->dma_done);

    rc = dma_start(cfg->dma_dev, cfg->dma_channel);
    if (rc) {
        LOG_ERR("dma_start ch%u failed: %d", (unsigned)cfg->dma_channel, rc);
        sys_clear_bit((mem_addr_t)cfg->dier_addr, cfg->dier_bit);
        return rc;
    }

    /* Wait for all bits (one CCR write per PWM period) */
    (void)k_sem_take(&data->dma_done, K_FOREVER);

    /* Stop DMA + disable peripheral DMA request */
    (void)dma_stop(cfg->dma_dev, cfg->dma_channel);
    sys_clear_bit((mem_addr_t)cfg->dier_addr, cfg->dier_bit);

    /* Latch (keep low for reset time) */
    (void)pwm_set_cycles(cfg->pwm.dev, cfg->pwm.channel, period_cyc, 0,
                         cfg->pwm.flags);
    k_busy_wait(WS2812_RESET_US);

    return 0;
}

static int ws2812_pwm_update_rgb(const struct device *dev,
                                 struct led_rgb *pixels, size_t num_pixels) {
    const struct ws2812_pwm_cfg *cfg = dev->config;

    if (!pixels || num_pixels == 0) {
        return 0;
    }

    const size_t need = (size_t)cfg->num_colors * num_pixels;
    if (need > 2048U) { /* keep stack reasonable; adjust if needed */
        LOG_ERR("pixel frame too large for stack buffer: %zu bytes", need);
        return -E2BIG;
    }
    uint8_t channels[need];

    for (size_t i = 0; i < num_pixels; ++i) {
        const struct led_rgb p = pixels[i];
        const size_t base = i * cfg->num_colors;
        for (uint8_t c = 0; c < cfg->num_colors; ++c) {
            switch (cfg->color_mapping[c]) {
            case LED_COLOR_ID_RED:
                channels[base + c] = p.r;
                break;
            case LED_COLOR_ID_GREEN:
                channels[base + c] = p.g;
                break;
            case LED_COLOR_ID_BLUE:
                channels[base + c] = p.b;
                break;
            case LED_COLOR_ID_WHITE:
                channels[base + c] = 0;
                break;
            default:
                channels[base + c] = 0;
                break;
            }
        }
    }

    return ws2812_pwm_update_channels(dev, channels, need);
}

static size_t ws2812_pwm_length(const struct device *dev) {
    const struct ws2812_pwm_cfg *cfg = dev->config;
    return cfg->length;
}

static DEVICE_API(led_strip, ws2812_pwm_api) = {
    .update_rgb = ws2812_pwm_update_rgb,
    .update_channels = ws2812_pwm_update_channels,
    .length = ws2812_pwm_length,
};

/* ---------------- Init ---------------- */

static int ws2812_pwm_init(const struct device *dev) {
    const struct ws2812_pwm_cfg *cfg = dev->config;
    struct ws2812_pwm_data *data = dev->data;

    if (!pwm_is_ready_dt(&cfg->pwm)) {
        LOG_ERR("PWM device not ready");
        return -ENODEV;
    }
    if (!device_is_ready(cfg->dma_dev)) {
        LOG_ERR("DMA device not ready");
        return -ENODEV;
    }
    if (cfg->ccr_addr == 0 || cfg->dier_addr == 0) {
        LOG_ERR("DT props ccr-addr/dier-addr must be set");
        return -EINVAL;
    }

    for (uint8_t i = 0; i < cfg->num_colors; i++) {
        switch (cfg->color_mapping[i]) {
        case LED_COLOR_ID_WHITE:
        case LED_COLOR_ID_RED:
        case LED_COLOR_ID_GREEN:
        case LED_COLOR_ID_BLUE:
            break;
        default:
            LOG_ERR("%s: invalid color mapping; check color-mapping DT",
                    dev->name);
            return -EINVAL;
        }
    }

    k_sem_init(&data->dma_done, 0, 1);

    /* Idle low */
    (void)pwm_set_cycles(cfg->pwm.dev, cfg->pwm.channel, 10, 0, cfg->pwm.flags);
    return 0;
}

/* ---------------- Instance macros ---------------- */

/* If your Zephyr is older and lacks BY_NAME variants, see note below. */

#define WS2812_COLOR_MAPPING(idx)                                              \
    static const uint8_t ws2812_pwm_##idx##_color_mapping[] =                  \
        DT_INST_PROP(idx, color_mapping)

#define WS2812_DMA_CTLR(idx)                                                   \
    DEVICE_DT_GET(DT_DMAS_CTLR_BY_NAME(DT_DRV_INST(idx), tx))
#define WS2812_DMA_CH(idx) DT_DMAS_CELL_BY_NAME(DT_DRV_INST(idx), tx, channel)
#define WS2812_DMA_SLOT(idx) DT_DMAS_CELL_BY_NAME(DT_DRV_INST(idx), tx, slot)

#define WS2812_PWM_DEFINE(idx)                                                 \
    WS2812_COLOR_MAPPING(idx);                                                 \
    static struct ws2812_pwm_data ws2812_pwm_##idx##_data;                     \
    static const struct ws2812_pwm_cfg ws2812_pwm_##idx##_cfg = {              \
        .pwm = PWM_DT_SPEC_GET(DT_DRV_INST(idx)),                              \
        .dma_dev = WS2812_DMA_CTLR(idx),                                       \
        .dma_channel = WS2812_DMA_CH(idx),                                     \
        .dma_slot = WS2812_DMA_SLOT(idx),                                      \
        .ccr_addr = DT_INST_PROP(idx, ccr_addr),                               \
        .dier_addr = DT_INST_PROP(idx, dier_addr),                             \
        .dier_bit = DT_INST_PROP(idx, dier_bit),                               \
        .color_mapping = ws2812_pwm_##idx##_color_mapping,                     \
        .length = DT_INST_PROP(idx, chain_length),                             \
        .num_colors = DT_INST_PROP_LEN(idx, color_mapping),                    \
    };                                                                         \
    DEVICE_DT_INST_DEFINE(idx, ws2812_pwm_init, NULL,                          \
                          &ws2812_pwm_##idx##_data, &ws2812_pwm_##idx##_cfg,   \
                          POST_KERNEL, CONFIG_LED_STRIP_INIT_PRIORITY,         \
                          &ws2812_pwm_api);

DT_INST_FOREACH_STATUS_OKAY(WS2812_PWM_DEFINE)

/* ---- If your tree doesn't have the BY_NAME macros: -----------------------
 * Replace the three WS2812_DMA_* defines above with the indexed forms:
 *
 * #define WS2812_DMA_CTLR(idx)  DEVICE_DT_GET(DT_DMAS_CTLR(DT_DRV_INST(idx),
 * 0)) #define WS2812_DMA_CH(idx)    DT_DMAS_CELL(DT_DRV_INST(idx), 0, channel)
 * #define WS2812_DMA_SLOT(idx)  DT_DMAS_CELL(DT_DRV_INST(idx), 0, slot)
 * ------------------------------------------------------------------------ */
