# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(kb_handle.c)

zephyr_linker_sources(SECTIONS iterables.ld)
