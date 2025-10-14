#ifndef DRIVERS_KSCAN_H_
#define DRIVERS_KSCAN_H_

#include <zephyr/device.h>
#include <zephyr/toolchain.h>

__subsystem struct kscan_driver_api {
    int (*poll_normal)(const struct device *dev, uint32_t *bitmap,
                       uint16_t *thresholds, uint16_t *values);
    int (*poll_race)(const struct device *dev, uint32_t *bitmap,
                     uint16_t *thresholds, uint16_t *values);
};

__syscall int kscan_poll_normal(const struct device *dev, uint32_t *bitmap,
                                uint16_t *thresholds, uint16_t *values);

static inline int z_impl_kscan_poll_normal(const struct device *dev,
                                           uint32_t *bitmap,
                                           uint16_t *thresholds,
                                           uint16_t *values) {
    __ASSERT_NO_MSG(DEVICE_API_IS(kscan, dev));

    return DEVICE_API_GET(kscan, dev)
        ->poll_normal(dev, bitmap, thresholds, values);
}

__syscall int kscan_poll_race(const struct device *dev, uint32_t *bitmap,
                              uint16_t *threshold, uint16_t *values);

static inline int z_impl_kscan_poll_race(const struct device *dev,
                                         uint32_t *bitmap, uint16_t *thresholds,
                                         uint16_t *values) {
    __ASSERT_NO_MSG(DEVICE_API_IS(kscan, dev));

    return DEVICE_API_GET(kscan, dev)
        ->poll_race(dev, bitmap, thresholds, values);
}

#include <syscalls/kscan.h>

#endif /* DRIVERS_BLINK_H_ */
