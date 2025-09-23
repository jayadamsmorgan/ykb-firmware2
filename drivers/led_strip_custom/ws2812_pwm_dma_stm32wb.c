/*
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ws2812_pwm_dma_stm32wb

#include <stdint.h>
#include <stm32wbxx_hal.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/devicetree/pwms.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/dt-bindings/led/led.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(ws2812_pwm_dma_stm32wb, CONFIG_LED_STRIP_LOG_LEVEL);

#define WS2812_T0H_NS 350U
#define WS2812_T1H_NS 700U
#define WS2812_RESET_US 80U

struct ws2812_pwm_cfg {
    uint32_t bit_rate;

    size_t dma_seq_len_max;

    uint8_t num_colors;
    const uint8_t *color_mapping;

    uint8_t dma_chan_irqn;

    size_t length;
};

struct ws2812_pwm_data {

    uint8_t tim_channel;

    TIM_HandleTypeDef htim;
    DMA_HandleTypeDef hdma;

    uint32_t ws_period_ticks;
    uint32_t ws_t0h_ticks;
    uint32_t ws_t1h_ticks;
    uint32_t ws_reset_slots;

    struct k_sem ws_sem;
    bool sem_release;

    uint32_t *dma_buff;

    size_t dma_seq_len;

    void (*post_init)(void);
};

static void ws_encode24(uint32_t grb24, uint32_t *out, size_t *pos,
                        uint32_t ws_t1h_ticks, uint32_t ws_t0h_ticks) {
    for (int bit = 23; bit >= 0; --bit) {
        bool one = (grb24 >> bit) & 1U;
        out[(*pos)++] = one ? ws_t1h_ticks : ws_t0h_ticks;
    }
}

static void ws_build_buffer(const struct device *dev, struct led_rgb *pixels,
                            size_t num_pixels) {
    const struct ws2812_pwm_cfg *cfg = dev->config;
    struct ws2812_pwm_data *data = dev->data;

    size_t p = 0;

    for (size_t i = 0; i < num_pixels; ++i) {
        uint32_t grb = ((uint32_t)pixels[i].g << 16) |
                       ((uint32_t)pixels[i].r << 8) |
                       ((uint32_t)pixels[i].b << 0);
        ws_encode24(grb, data->dma_buff, &p, data->ws_t1h_ticks,
                    data->ws_t0h_ticks);
    }

    /* Reset tail: keep line low by producing 0-high pulses for many bit slots.
       For PWM1 mode, CCR=0 → output stays low for entire slot. */
    for (uint32_t i = 0; i < data->ws_reset_slots; ++i) {
        data->dma_buff[p++] = 0;
    }

    data->dma_seq_len = MIN(p, cfg->dma_seq_len_max);
}

/* Build DMA buffer directly from channel array using DT color_mapping. */
static void ws_build_buffer_channels(const struct device *dev,
                                     const uint8_t *ch, size_t nch) {
    const struct ws2812_pwm_cfg *cfg = dev->config;
    struct ws2812_pwm_data *data = dev->data;

    size_t p = 0;
    const uint8_t ncol = cfg->num_colors; /* e.g. 3 */
    const size_t maxp = cfg->length;

    /* Each LED consumes ncol channel values, ordered per color_mapping */
    size_t leds = nch / ncol;
    if (leds > maxp)
        leds = maxp;

    for (size_t i = 0; i < leds; ++i) {
        const uint8_t *px = &ch[i * ncol];

        /* Map into GRB 24-bit word as the WS2812 encoder expects (G,R,B). */
        /* color_mapping entries are LED_COLOR_ID_* values. */
        uint8_t r = 0, g = 0, b = 0;
        for (uint8_t k = 0; k < ncol; ++k) {
            uint8_t id = cfg->color_mapping[k];
            switch (id) {
            case LED_COLOR_ID_RED:
                r = px[k];
                break;
            case LED_COLOR_ID_GREEN:
                g = px[k];
                break;
            case LED_COLOR_ID_BLUE:
                b = px[k];
                break;
            default: /* ignore unsupported colors */
                break;
            }
        }

        uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | (uint32_t)b;
        ws_encode24(grb, data->dma_buff, &p, data->ws_t1h_ticks,
                    data->ws_t0h_ticks);
    }

    /* Reset tail keeps line low */
    for (uint32_t i = 0; i < data->ws_reset_slots; ++i) {
        data->dma_buff[p++] = 0;
    }

    data->dma_seq_len = MIN(p, cfg->dma_seq_len_max);
}

/* Kick DMA transfer of ws_buf[] into CCR4 each update event. */
static void ws_start_dma(const struct device *dev) {
    struct ws2812_pwm_data *data = dev->data;

    /* Ensure CCR starts at 0 (idle low) before streaming */
    __HAL_TIM_SET_COMPARE(&data->htim, data->tim_channel, 0);

    /* Start PWM + DMA: HAL will trigger DMA on CC4 request to load CCR4 */
    HAL_TIM_PWM_Start_DMA((TIM_HandleTypeDef *)&data->htim, data->tim_channel,
                          (uint32_t *)data->dma_buff,
                          (uint16_t)data->dma_seq_len);
}

static int ws2812_pwm_update_rgb(const struct device *dev,
                                 struct led_rgb *pixels, size_t num_pixels) {
    struct ws2812_pwm_data *data = dev->data;

    ws_build_buffer(dev, pixels, num_pixels);
    k_sem_reset(&data->ws_sem);
    data->sem_release = false;
    ws_start_dma(dev);
    k_sem_take(&data->ws_sem, K_FOREVER);
    HAL_TIM_PWM_Stop_DMA(&data->htim, data->tim_channel);
    return 0;
}

static int ws2812_pwm_update_channels(const struct device *dev,
                                      uint8_t *channels, size_t num_channels) {
    ws_build_buffer_channels(dev, channels, num_channels);
    ws_start_dma(dev);
    return 0;
}

static unsigned int ws2812_pwm_length(const struct device *dev) {
    const struct ws2812_pwm_cfg *cfg = dev->config;
    return cfg->length;
}

static DEVICE_API(led_strip, ws2812_pwm_api) = {
    .length = ws2812_pwm_length,
    .update_channels = ws2812_pwm_update_channels,
    .update_rgb = ws2812_pwm_update_rgb,
};

static uint32_t get_tim2_input_clk_hz(void) {
    /* TIM2 is on APB1. When APB1 prescaler > 1, timer clock is PCLK1 * 2. */
    RCC_ClkInitTypeDef clk = {0};
    uint32_t flash_latency;
    HAL_RCC_GetClockConfig(&clk, &flash_latency);

    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    uint32_t apb1_div = clk.APB1CLKDivider; // RCC_HCLK_DIVx
    bool apb1_div_greater_than_1 = (apb1_div != RCC_HCLK_DIV1);
    return apb1_div_greater_than_1 ? (pclk1 * 2U) : pclk1;
}

static uint32_t ns_to_ticks(uint32_t ns, uint32_t timclk_hz) {
    /* ticks = round(ns * timclk / 1e9) */
    uint64_t t =
        ((uint64_t)ns * (uint64_t)timclk_hz + 500000000ULL) / 1000000000ULL;
    if (t == 0)
        t = 1;
    return (uint32_t)t;
}

static void ws_compute_timings(const struct device *dev) {
    const struct ws2812_pwm_cfg *cfg = dev->config;
    struct ws2812_pwm_data *data = dev->data;

    uint32_t timclk = get_tim2_input_clk_hz();

    /* Period ticks (ARR+1) to hit ~800kHz: */
    data->ws_period_ticks =
        (uint32_t)((timclk + (cfg->bit_rate / 2)) / cfg->bit_rate);
    if (data->ws_period_ticks < 2)
        data->ws_period_ticks = 2;

    data->ws_t0h_ticks = ns_to_ticks(WS2812_T0H_NS, timclk);
    data->ws_t1h_ticks = ns_to_ticks(WS2812_T1H_NS, timclk);

    if (data->ws_t0h_ticks >= data->ws_period_ticks)
        data->ws_t0h_ticks = data->ws_period_ticks - 1;
    if (data->ws_t1h_ticks >= data->ws_period_ticks)
        data->ws_t1h_ticks = data->ws_period_ticks - 1;

    /* Reset (latch) slots = time/1.25us: */
    data->ws_reset_slots =
        (uint32_t)(((uint64_t)WS2812_RESET_US * 1000ULL + 1249ULL) / 1250ULL);
    if (data->ws_reset_slots < 64)
        data->ws_reset_slots = 64; // safe margin

    /* --- (Re)configure TIM2 quickly with computed ARR --- */
    TIM_HandleTypeDef *htim = (TIM_HandleTypeDef *)&data->htim;

    /* Stop if running */
    HAL_TIM_PWM_Stop(htim, data->tim_channel);

    /* Base init (no prescaler; we derive exact ARR) */
    htim->Init.Prescaler = 0;
    htim->Init.CounterMode = TIM_COUNTERMODE_UP;
    htim->Init.Period = data->ws_period_ticks - 1; // ARR
    htim->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_PWM_Init(htim);

    /* PWM CH4 config */
    TIM_OC_InitTypeDef sConfigOC = {0};
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0; // start low
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(htim, &sConfigOC, data->tim_channel);

    /* Enable PWM output (no DMA yet) */
    HAL_TIM_PWM_Start(htim, data->tim_channel);
    HAL_TIM_PWM_Stop(htim, data->tim_channel);
}

static void ws2812_pwm_dma_isr(const void *arg) {
    struct ws2812_pwm_data *data = (struct ws2812_pwm_data *)arg;
    if (data->sem_release) {
        k_sem_give(&data->ws_sem);
    } else {
        data->sem_release = true;
    }
}

static int ws2812_pwm_init(const struct device *dev) {
    struct ws2812_pwm_data *data = dev->data;

    int ret;

    ret = HAL_TIM_PWM_Init((TIM_HandleTypeDef *)&data->htim);
    if (ret != HAL_OK) {
        LOG_ERR("TIM init error %d", ret);
        return -ENODEV;
    }

    TIM_MasterConfigTypeDef sMasterConfig = {0};

    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    ret = HAL_TIMEx_MasterConfigSynchronization(
        (TIM_HandleTypeDef *)&data->htim, &sMasterConfig);
    if (ret != HAL_OK) {
        LOG_ERR("TIMEx_MasterConfigSync error %d", ret);
        return -ENODEV;
    }

    ret = HAL_DMA_Init((DMA_HandleTypeDef *)&data->hdma);
    if (ret != HAL_OK) {
        LOG_ERR("DMA init error %d", ret);
        return -ENODEV;
    }

    ws_compute_timings(dev);

    k_sem_init(&data->ws_sem, 0, 1);

    if (data->post_init) {
        data->post_init();
    }

    return 0;
}

#define WS2812_COLOR_MAPPING(idx)                                              \
    static const uint8_t ws2812_pwm_##idx##_color_mapping[] =                  \
        DT_INST_PROP(idx, color_mapping)

#define WS2812_DMA_BUFFER(idx, led_count)                                      \
    static uint32_t ws2812_pwm_##idx##_dma_buffer[led_count * 24 + 256]

#define WS2812_POST_INIT_FN(idx) static void ws2812_pwm_##idx##_post_init(void)

#define WS2812_POST_INIT_FN_DECL(idx)                                          \
    WS2812_POST_INIT_FN(idx) {                                                 \
        IRQ_CONNECT(DT_INST_PROP(idx, st_dma_chan_irqn), 0,                    \
                    ws2812_pwm_dma_isr, &ws2812_pwm_##idx##_data, 0);          \
    }

#define WS2812_PWM_DEFINE(idx)                                                 \
    WS2812_COLOR_MAPPING(idx);                                                 \
    WS2812_DMA_BUFFER(idx, DT_INST_PROP(idx, chain_length));                   \
    WS2812_POST_INIT_FN(idx);                                                  \
    static struct ws2812_pwm_data ws2812_pwm_##idx##_data = {                  \
        .dma_buff = ws2812_pwm_##idx##_dma_buffer,                             \
        .tim_channel = DT_INST_PROP(idx, st_tim_channel),                      \
        .post_init = ws2812_pwm_##idx##_post_init,                             \
        .hdma =                                                                \
            {                                                                  \
                .Instance = (DMA_Channel_TypeDef *)DT_INST_PROP(               \
                    idx, st_dma_chan_inst),                                    \
                .Init =                                                        \
                    {                                                          \
                        .Request = DT_INST_PROP(idx, st_dma_req),              \
                        .Direction = DMA_MEMORY_TO_PERIPH,                     \
                        .PeriphInc = DMA_PINC_DISABLE,                         \
                        .MemInc = DMA_MINC_ENABLE,                             \
                        .PeriphDataAlignment = DMA_PDATAALIGN_WORD,            \
                        .MemDataAlignment = DMA_MDATAALIGN_WORD,               \
                        .Mode = DMA_NORMAL,                                    \
                        .Priority = DMA_PRIORITY_MEDIUM,                       \
                    },                                                         \
                .Parent = (TIM_HandleTypeDef *)&ws2812_pwm_##idx##_data.htim,  \
            },                                                                 \
        .htim =                                                                \
            {                                                                  \
                .Instance = (TIM_TypeDef *)DT_INST_PROP(idx, st_tim_base),     \
                .Init =                                                        \
                    {                                                          \
                        .Prescaler = 0,                                        \
                        .Period = 4294967295,                                  \
                        .CounterMode = TIM_COUNTERMODE_UP,                     \
                        .ClockDivision = TIM_CLOCKDIVISION_DIV1,               \
                        .AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE,   \
                    },                                                         \
                .hdma[DT_INST_PROP(idx, st_tim_dma_id)] =                      \
                    (DMA_HandleTypeDef *)&ws2812_pwm_##idx##_data.hdma,        \
            },                                                                 \
    };                                                                         \
    WS2812_POST_INIT_FN_DECL(idx)                                              \
    static const struct ws2812_pwm_cfg ws2812_pwm_##idx##_cfg = {              \
        .color_mapping = ws2812_pwm_##idx##_color_mapping,                     \
        .dma_chan_irqn = DT_INST_PROP(idx, st_dma_chan_irqn),                  \
        .length = DT_INST_PROP(idx, chain_length),                             \
        .bit_rate = DT_INST_PROP(idx, bit_rate),                               \
        .num_colors = DT_INST_PROP_LEN(idx, color_mapping),                    \
        .dma_seq_len_max = DT_INST_PROP(idx, chain_length) * 24 + 256,         \
    };                                                                         \
    DEVICE_DT_INST_DEFINE(idx, ws2812_pwm_init, NULL,                          \
                          &ws2812_pwm_##idx##_data, &ws2812_pwm_##idx##_cfg,   \
                          POST_KERNEL, CONFIG_LED_STRIP_INIT_PRIORITY,         \
                          &ws2812_pwm_api);

DT_INST_FOREACH_STATUS_OKAY(WS2812_PWM_DEFINE)
