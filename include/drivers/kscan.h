#ifndef DRIVERS_KSCAN_H_
#define DRIVERS_KSCAN_H_

#include <zephyr/device.h>
#include <zephyr/toolchain.h>

__subsystem struct kscan_driver_api {
    int (*poll_normal)(const struct device *dev, bool *pressed_keys,
                       uint16_t threshold);
    int (*poll_race)(const struct device *dev, uint16_t threshold);
};

__syscall int kscan_poll_normal(const struct device *dev, bool *pressed_keys,
                                uint16_t threshold);

static inline int z_impl_kscan_poll_normal(const struct device *dev,
                                           bool *pressed_keys,
                                           uint16_t threshold) {
    __ASSERT_NO_MSG(DEVICE_API_IS(kscan, dev));

    return DEVICE_API_GET(kscan, dev)
        ->poll_normal(dev, pressed_keys, threshold);
}

__syscall int kscan_poll_race(const struct device *dev, uint16_t threshold);

static inline int z_impl_kscan_poll_race(const struct device *dev,
                                         uint16_t threshold) {
    __ASSERT_NO_MSG(DEVICE_API_IS(kscan, dev));

    return DEVICE_API_GET(kscan, dev)->poll_race(dev, threshold);
}

#include <syscalls/kscan.h>

#endif /* DRIVERS_BLINK_H_ */
