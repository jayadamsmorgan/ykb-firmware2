zephyr_library()

zephyr_library_sources_ifdef(CONFIG_MUX_GPIO mux_gpio.c)
