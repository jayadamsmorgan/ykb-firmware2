#ifndef APP_DRIVERS_KSCAN_H_
#define APP_DRIVERS_KSCAN_H_

#include <zephyr/device.h>
#include <zephyr/toolchain.h>

__subsystem struct kscan_driver_api {
    int (*poll_normal)(const struct device *dev, uint8_t *pressed_keys,
                       size_t max_pressed_keys);
    int (*poll_race)(const struct device *dev);
};

__syscall int kscan_poll_normal(const struct device *dev, uint8_t *pressed_keys,
                                size_t max_pressed_keys);

static inline int z_impl_kscan_poll_normal(const struct device *dev,
                                           uint8_t *pressed_keys,
                                           size_t max_pressed_keys) {
    __ASSERT_NO_MSG(DEVICE_API_IS(kscan, dev));

    return DEVICE_API_GET(kscan, dev)
        ->poll_normal(dev, pressed_keys, max_pressed_keys);
}

__syscall int kscan_poll_race(const struct device *dev);

static inline int z_impl_kscan_poll_race(const struct device *dev) {
    __ASSERT_NO_MSG(DEVICE_API_IS(kscan, dev));

    return DEVICE_API_GET(kscan, dev)->poll_race(dev);
}

#include <syscalls/kscan.h>

#endif /* APP_DRIVERS_BLINK_H_ */
