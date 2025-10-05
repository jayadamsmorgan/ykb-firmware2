zephyr_library()

zephyr_library_sources_ifdef(CONFIG_KB_BACKLIGHT_DEVICE_LED kb_backlight_led.c)

zephyr_library_sources_ifdef(CONFIG_KB_BACKLIGHT_DEVICE_LED_STRIP kb_backlight_led_strip.c)

zephyr_compile_definitions(YKB_LEDS_GEOM_PATH=<lib/led/geom/${BOARD}.h>)

zephyr_linker_sources(SECTIONS iterables.ld)
