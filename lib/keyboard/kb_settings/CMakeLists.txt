# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(kb_settings.c)

zephyr_compile_definitions(YKB_DEF_MAPPINGS_PATH=<lib/keyboard/mappings/${BOARD}.h>)
