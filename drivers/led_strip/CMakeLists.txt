zephyr_library()

zephyr_library_sources_ifdef(CONFIG_WS2812_STRIP_PWM_DMA_STM32 ws2812_pwm.c)

