# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(kb_handle_common.c)

zephyr_library_sources_ifdef(CONFIG_KB_HANDLE_IMPL_NORMAL kb_handle_normal.c)
zephyr_library_sources_ifdef(CONFIG_KB_HANDLE_IMPL_SLAVE kb_handle_slave.c)
zephyr_library_sources_ifdef(CONFIG_KB_HANDLE_IMPL_MASTER kb_handle_master.c)

zephyr_compile_definitions(YKB_FN_KEYSTROKES_PATH=<lib/keyboard/keystrokes/${BOARD}.h>)

zephyr_linker_sources(SECTIONS iterables.ld)
