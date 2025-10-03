# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(bt_connect.c)

zephyr_library_sources_ifdef(
    CONFIG_BT_INTER_KB_COMM
    inter_kb_comm/inter_kb_comm.c
)

zephyr_library_sources_ifdef(
    CONFIG_BT_INTER_KB_COMM_MASTER
    inter_kb_comm/master.c
)

zephyr_library_sources_ifdef(
    CONFIG_BT_INTER_KB_COMM_SLAVE
    inter_kb_comm/slave.c
)
