#ifndef ZEPHYR_DRIVERS_MISC_CSRF_H_
#define ZEPHYR_DRIVERS_MISC_CSRF_H_

#include <stdint.h>

struct csrf_channel_data
{
    uint16_t ch[16];
};

typedef void (*csrf_channel_callback_t)(
    const struct csrf_channel_data *data);

struct csrf_driver_api
{
    int (*set_channel_callback)(const struct device *dev,
                                csrf_channel_callback_t callback);
};

static inline int csrf_set_channel_callback(
    const struct device *dev,
    csrf_channel_callback_t callback)
{
    const struct csrf_driver_api *api = dev->api;

    if (api == NULL || api->set_channel_callback == NULL)
    {
        return -ENOTSUP;
    }

    return api->set_channel_callback(dev, callback);
}

#endif
