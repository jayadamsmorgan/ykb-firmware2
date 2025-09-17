#ifndef USB_CONNECT_USBD_INIT_H
#define USB_CONNECT_USBD_INIT_H

#include <stdint.h>
#include <zephyr/usb/usbd.h>

struct usbd_context *usbd_init_device(usbd_msg_cb_t msg_cb);

struct usbd_context *usbd_setup_device(usbd_msg_cb_t msg_cb);

#endif /* USB_CONNECT_USBD_INIT_H */
