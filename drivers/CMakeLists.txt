# SPDX-License-Identifier: Apache-2.0

add_subdirectory_ifdef(CONFIG_KSCAN kscan)

add_subdirectory_ifdef(CONFIG_MUX mux)

add_subdirectory_ifdef(CONFIG_LED_STRIP led_strip)
