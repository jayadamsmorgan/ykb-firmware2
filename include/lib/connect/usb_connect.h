// SPDX-License-Identifier: Apache-2.0

#ifndef LIB_USB_CONNECT_H_
#define LIB_USB_CONNECT_H_

#include <stdbool.h>
#include <stdint.h>

#define USB_CONNECT_HID_REPORT_COUNT 8

int usb_connect_init();

void usb_connect_send(uint8_t buffer[USB_CONNECT_HID_REPORT_COUNT]);

void usb_connect_handle_wakeup();

bool usb_connect_is_ready();

uint32_t usb_connect_duration();

#endif // LIB_USB_CONNECT_H_
