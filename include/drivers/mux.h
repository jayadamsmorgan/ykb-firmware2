#ifndef DRIVERS_MUX_H_
#define DRIVERS_MUX_H_

#include <zephyr/device.h>
#include <zephyr/toolchain.h>

__subsystem struct mux_driver_api {

    int (*select)(const struct device *dev, unsigned int channel);
    int (*select_next)(const struct device *dev);

    int (*get_current_channel)(const struct device *dev);
    int (*get_channel_amount)(const struct device *dev);

    int (*enable)(const struct device *dev);
    int (*disable)(const struct device *dev);

    int (*is_enabled)(const struct device *dev);
};

__syscall int mux_select(const struct device *dev, unsigned int channel);

static inline int z_impl_mux_select(const struct device *dev,
                                    unsigned int channel) {
    __ASSERT_NO_MSG(DEVICE_API_IS(mux, dev));

    return DEVICE_API_GET(mux, dev)->select(dev, channel);
}

__syscall int mux_select_next(const struct device *dev);

static inline int z_impl_mux_select_next(const struct device *dev) {
    __ASSERT_NO_MSG(DEVICE_API_IS(mux, dev));

    return DEVICE_API_GET(mux, dev)->select_next(dev);
}

__syscall int mux_get_current_channel(const struct device *dev);

static inline int z_impl_mux_get_current_channel(const struct device *dev) {
    __ASSERT_NO_MSG(DEVICE_API_IS(mux, dev));

    return DEVICE_API_GET(mux, dev)->get_current_channel(dev);
}

__syscall int mux_get_channel_amount(const struct device *dev);

static inline int z_impl_mux_get_channel_amount(const struct device *dev) {
    __ASSERT_NO_MSG(DEVICE_API_IS(mux, dev));

    return DEVICE_API_GET(mux, dev)->get_channel_amount(dev);
}

__syscall int mux_enable(const struct device *dev);

static inline int z_impl_mux_enable(const struct device *dev) {

    __ASSERT_NO_MSG(DEVICE_API_IS(mux, dev));

    return DEVICE_API_GET(mux, dev)->enable(dev);
}

__syscall int mux_disable(const struct device *dev);

static inline int z_impl_mux_disable(const struct device *dev) {

    __ASSERT_NO_MSG(DEVICE_API_IS(mux, dev));

    return DEVICE_API_GET(mux, dev)->disable(dev);
}

__syscall int mux_is_enabled(const struct device *dev);

static inline int z_impl_mux_is_enabled(const struct device *dev) {

    __ASSERT_NO_MSG(DEVICE_API_IS(mux, dev));

    return DEVICE_API_GET(mux, dev)->is_enabled(dev);
}

#include <syscalls/mux.h>

#endif /* DRIVERS_MUX_H_ */
