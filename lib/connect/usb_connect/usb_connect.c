#include <lib/connect/usb_connect.h>

#include "usbd_init.h"

#include <zephyr/usb/class/hid.h>
#include <zephyr/usb/class/usbd_hid.h>

LOG_MODULE_REGISTER(usb_connect, CONFIG_USB_CONNECT_LOG_LEVEL);

static const uint8_t hid_report_desc[] = HID_KEYBOARD_REPORT_DESC();

static uint32_t kb_duration;
static bool kb_ready;

static struct usbd_context *usbd;
static const struct device *hid_dev;

static void kb_iface_ready(const struct device *dev, const bool ready) {
    LOG_INF("HID device %s interface is %s", dev->name,
            ready ? "ready" : "not ready");
    kb_ready = ready;
}

static int kb_get_report(const struct device *dev, const uint8_t type,
                         const uint8_t id, const uint16_t len,
                         uint8_t *const buf) {
    LOG_WRN("Get Report not implemented, Type %u ID %u", type, id);
    return 0;
}

static int kb_set_report(const struct device *dev, const uint8_t type,
                         const uint8_t id, const uint16_t len,
                         const uint8_t *const buf) {
    if (type != HID_REPORT_TYPE_OUTPUT) {
        LOG_WRN("Unsupported report type");
        return -ENOTSUP;
    }

    return 0;
}

/* Idle duration is stored but not used to calculate idle reports. */
static void kb_set_idle(const struct device *dev, const uint8_t id,
                        const uint32_t duration) {
    LOG_INF("Set Idle %u to %u", id, duration);
    kb_duration = duration;
}

static uint32_t kb_get_idle(const struct device *dev, const uint8_t id) {
    LOG_INF("Get Idle %u to %u", id, kb_duration);
    return kb_duration;
}

static void kb_set_protocol(const struct device *dev, const uint8_t proto) {
    LOG_INF("Protocol changed to %s",
            proto == 0U ? "Boot Protocol" : "Report Protocol");
}

static void kb_output_report(const struct device *dev, const uint16_t len,
                             const uint8_t *const buf) {
    LOG_HEXDUMP_DBG(buf, len, "o.r.");
    kb_set_report(dev, HID_REPORT_TYPE_OUTPUT, 0U, len, buf);
}

struct hid_device_ops kb_ops = {
    .iface_ready = kb_iface_ready,
    .get_report = kb_get_report,
    .set_report = kb_set_report,
    .set_idle = kb_set_idle,
    .get_idle = kb_get_idle,
    .set_protocol = kb_set_protocol,
    .output_report = kb_output_report,
};

static void msg_cb(struct usbd_context *const usbd_ctx,
                   const struct usbd_msg *const msg) {
    LOG_INF("USBD message: %s", usbd_msg_type_string(msg->type));

    if (msg->type == USBD_MSG_CONFIGURATION) {
        LOG_INF("\tConfiguration value %d", msg->status);
    }

    if (usbd_can_detect_vbus(usbd_ctx)) {
        if (msg->type == USBD_MSG_VBUS_READY) {
            if (usbd_enable(usbd_ctx)) {
                LOG_ERR("Failed to enable device support");
            }
        }

        if (msg->type == USBD_MSG_VBUS_REMOVED) {
            if (usbd_disable(usbd_ctx)) {
                LOG_ERR("Failed to disable device support");
            }
        }
    }
}

int usb_connect_init() {
    int ret;

    hid_dev = DEVICE_DT_GET_ONE(zephyr_hid_device);
    if (!device_is_ready(hid_dev)) {
        return -EIO;
    }

    ret = hid_device_register(hid_dev, hid_report_desc, sizeof(hid_report_desc),
                              &kb_ops);
    if (ret != 0) {
        return ret;
    }

    usbd = usbd_init_device(msg_cb);
    if (usbd == NULL) {
        return -ENODEV;
    }

    if (!usbd_can_detect_vbus(usbd)) {
        ret = usbd_enable(usbd);
        if (ret) {
            return ret;
        }
    }

    return 0;
}

void usb_connect_handle_wakeup() {
    if (IS_ENABLED(CONFIG_YKB_USBD_REMOTE_WAKEUP) && usbd_is_suspended(usbd)) {
        int ret = usbd_wakeup_request(usbd);
        if (ret) {
            LOG_ERR("Remote wakeup error, %d", ret);
        }
    }
}

void usb_connect_send(uint8_t buffer[USB_CONNECT_HID_REPORT_COUNT]) {
    int ret =
        hid_device_submit_report(hid_dev, USB_CONNECT_HID_REPORT_COUNT, buffer);
    if (ret) {
        LOG_ERR("HID submit report error, %d", ret);
    }
}

bool usb_connect_is_ready() {
    return kb_ready;
}

uint32_t usb_connect_duration() {
    return kb_duration;
}
