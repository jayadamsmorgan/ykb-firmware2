#include "zephyr/dfu/mcuboot.h"
#include <lib/connect/usb_connect.h>

#include <zephyr/logging/log.h>

#include <zephyr/usb/class/hid.h>
#include <zephyr/usb/class/usbd_hid.h>
#include <zephyr/usb/usbd.h>

LOG_MODULE_REGISTER(usb_connect, CONFIG_USB_CONNECT_LOG_LEVEL);

static const uint8_t hid_report_desc[] = HID_KEYBOARD_REPORT_DESC();

static uint32_t kb_duration;
static bool kb_ready;

static const char *const blocklist[] = {
    NULL,
};

USBD_DEVICE_DEFINE(usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
                   CONFIG_YKB_USBD_VID, CONFIG_YKB_USBD_PID);

USBD_DESC_LANG_DEFINE(lang);
USBD_DESC_MANUFACTURER_DEFINE(mfr, CONFIG_YKB_USBD_MANUFACTURER);
USBD_DESC_PRODUCT_DEFINE(product, CONFIG_YKB_USBD_PRODUCT);
IF_ENABLED(CONFIG_HWINFO, (USBD_DESC_SERIAL_NUMBER_DEFINE(sn)));

USBD_DESC_CONFIG_DEFINE(fs_cfg_desc, "FS Configuration");
USBD_DESC_CONFIG_DEFINE(hs_cfg_desc, "HS Configuration");

static const uint8_t attributes =
    (IS_ENABLED(CONFIG_YKB_USBD_SELF_POWERED) ? USB_SCD_SELF_POWERED : 0) |
    (IS_ENABLED(CONFIG_YKB_USBD_REMOTE_WAKEUP) ? USB_SCD_REMOTE_WAKEUP : 0);

/* Full speed configuration */
USBD_CONFIGURATION_DEFINE(fs_config, attributes, CONFIG_YKB_USBD_MAX_POWER,
                          &fs_cfg_desc);

/* High speed configuration */
USBD_CONFIGURATION_DEFINE(hs_config, attributes, CONFIG_YKB_USBD_MAX_POWER,
                          &hs_cfg_desc);

#if CONFIG_YKB_USBD_20_EXTENSION_DESC

static const struct usb_bos_capability_lpm bos_cap_lpm = {
    .bLength = sizeof(struct usb_bos_capability_lpm),
    .bDescriptorType = USB_DESC_DEVICE_CAPABILITY,
    .bDevCapabilityType = USB_BOS_CAPABILITY_EXTENSION,
    .bmAttributes = 0UL,
};

USBD_DESC_BOS_DEFINE(usbext, sizeof(bos_cap_lpm), &bos_cap_lpm);

#endif // CONFIG_YKB_USBD_20_EXTENSION_DESC

static void fix_code_triple(struct usbd_context *uds_ctx,
                            const enum usbd_speed speed) {
    if (IS_ENABLED(CONFIG_USBD_CDC_ACM_CLASS) ||
        IS_ENABLED(CONFIG_USBD_CDC_ECM_CLASS) ||
        IS_ENABLED(CONFIG_USBD_CDC_NCM_CLASS) ||
        IS_ENABLED(CONFIG_USBD_MIDI2_CLASS) ||
        IS_ENABLED(CONFIG_USBD_AUDIO2_CLASS) ||
        IS_ENABLED(CONFIG_USBD_VIDEO_CLASS)) {
        usbd_device_set_code_triple(uds_ctx, speed, USB_BCC_MISCELLANEOUS, 0x02,
                                    0x01);
    } else {
        usbd_device_set_code_triple(uds_ctx, speed, 0, 0, 0);
    }
}

struct usbd_context *usbd_setup_device(usbd_msg_cb_t msg_cb) {
    int err;

    err = usbd_add_descriptor(&usbd, &lang);
    if (err) {
        LOG_ERR("Failed to initialize language descriptor (%d)", err);
        return NULL;
    }

    err = usbd_add_descriptor(&usbd, &mfr);
    if (err) {
        LOG_ERR("Failed to initialize manufacturer descriptor (%d)", err);
        return NULL;
    }

    err = usbd_add_descriptor(&usbd, &product);
    if (err) {
        LOG_ERR("Failed to initialize product descriptor (%d)", err);
        return NULL;
    }

    IF_ENABLED(CONFIG_HWINFO, (err = usbd_add_descriptor(&usbd, &sn);))
    if (err) {
        LOG_ERR("Failed to initialize SN descriptor (%d)", err);
        return NULL;
    }

    if (USBD_SUPPORTS_HIGH_SPEED && usbd_caps_speed(&usbd) == USBD_SPEED_HS) {
        err = usbd_add_configuration(&usbd, USBD_SPEED_HS, &hs_config);
        if (err) {
            LOG_ERR("Failed to add High-Speed configuration");
            return NULL;
        }

        err = usbd_register_all_classes(&usbd, USBD_SPEED_HS, 1, blocklist);
        if (err) {
            LOG_ERR("Failed to add register classes");
            return NULL;
        }

        fix_code_triple(&usbd, USBD_SPEED_HS);
    }

    err = usbd_add_configuration(&usbd, USBD_SPEED_FS, &fs_config);
    if (err) {
        LOG_ERR("Failed to add Full-Speed configuration");
        return NULL;
    }

    err = usbd_register_all_classes(&usbd, USBD_SPEED_FS, 1, blocklist);
    if (err) {
        LOG_ERR("Failed to add register classes");
        return NULL;
    }

    fix_code_triple(&usbd, USBD_SPEED_FS);
    usbd_self_powered(&usbd, attributes & USB_SCD_SELF_POWERED);

    if (msg_cb != NULL) {
        err = usbd_msg_register_cb(&usbd, msg_cb);
        if (err) {
            LOG_ERR("Failed to register message callback");
            return NULL;
        }
    }

#if CONFIG_YKB_USBD_20_EXTENSION_DESC

    (void)usbd_device_set_bcd_usb(&usbd, USBD_SPEED_FS, 0x0201);
    (void)usbd_device_set_bcd_usb(&usbd, USBD_SPEED_HS, 0x0201);

    err = usbd_add_descriptor(&usbd, &usbext);
    if (err) {
        LOG_ERR("Failed to add USB 2.0 Extension Descriptor");
        return NULL;
    }

#endif // CONFIG_YKB_USBD_20_EXTENSION_DESC

    return &usbd;
}

struct usbd_context *usbd_init_device(usbd_msg_cb_t msg_cb) {
    int err;

    if (usbd_setup_device(msg_cb) == NULL) {
        return NULL;
    }

    err = usbd_init(&usbd);
    if (err) {
        LOG_ERR("Failed to initialize device support");
        return NULL;
    }

    return &usbd;
}
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
                   const struct usbd_msg *const msg);

static void switch_to_dfu_mode(struct usbd_context *const ctx) {
    int err;

    LOG_INF("Detach USB device");
    usbd_disable(ctx);
    usbd_shutdown(ctx);

    err = usbd_add_descriptor(&usbd, &lang);
    if (err) {
        LOG_ERR("Failed to initialize language descriptor (%d)", err);
        return;
    }

    if (usbd_caps_speed(&usbd) == USBD_SPEED_HS) {
        err = usbd_add_configuration(&usbd, USBD_SPEED_HS, &hs_config);
        if (err) {
            LOG_ERR("Failed to add High-Speed configuration");
            return;
        }

        err = usbd_register_class(&usbd, "dfu_dfu", USBD_SPEED_HS, 1);
        if (err) {
            LOG_ERR("Failed to add register classes");
            return;
        }

        usbd_device_set_code_triple(&usbd, USBD_SPEED_HS, 0, 0, 0);
    }

    err = usbd_add_configuration(&usbd, USBD_SPEED_FS, &fs_config);
    if (err) {
        LOG_ERR("Failed to add Full-Speed configuration");
        return;
    }

    err = usbd_register_class(&usbd, "dfu_dfu", USBD_SPEED_FS, 1);
    if (err) {
        LOG_ERR("Failed to add register classes");
        return;
    }

    usbd_device_set_code_triple(&usbd, USBD_SPEED_FS, 0, 0, 0);

    err = usbd_init(&usbd);
    if (err) {
        LOG_ERR("Failed to initialize USB device support");
        return;
    }

    // err = usbd_msg_register_cb(&usbd, msg_cb);
    // if (err) {
    //     LOG_ERR("Failed to register message callback");
    //     return;
    // }

    err = usbd_enable(&usbd);
    if (err) {
        LOG_ERR("Failed to enable USB device support");
    }
}

static void msg_cb(struct usbd_context *const usbd_ctx,
                   const struct usbd_msg *const msg) {
    LOG_INF("USBD message: %s", usbd_msg_type_string(msg->type));

    if (msg->type == USBD_MSG_CONFIGURATION) {
        LOG_INF("Configuration value %d", msg->status);
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

    if (msg->type == USBD_MSG_DFU_APP_DETACH) {
        switch_to_dfu_mode(usbd_ctx);
    }

    if (msg->type == USBD_MSG_DFU_DOWNLOAD_COMPLETED) {
        if (IS_ENABLED(CONFIG_BOOTLOADER_MCUBOOT) &&
            IS_ENABLED(CONFIG_APP_USB_DFU_USE_FLASH_BACKEND)) {
            boot_request_upgrade(false);
        }
    }
}

static const struct device *hid_dev;

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

    struct usbd_context *usbd = usbd_init_device(msg_cb);
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
    if (IS_ENABLED(CONFIG_YKB_USBD_REMOTE_WAKEUP) && usbd_is_suspended(&usbd)) {
        int ret = usbd_wakeup_request(&usbd);
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
