# SPDX-License-Identifier: Apache-2.0

add_subdirectory_ifdef(CONFIG_LIB_USB_CONNECT usb_connect)

add_subdirectory_ifdef(CONFIG_LIB_BT_CONNECT bt_connect)
