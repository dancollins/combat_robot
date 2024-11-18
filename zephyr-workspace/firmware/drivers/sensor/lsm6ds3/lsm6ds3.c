#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lsm6ds3, CONFIG_SENSOR_LOG_LEVEL);

#define DT_DRV_COMPAT st_lsm6ds3

struct lsm6ds3_config
{
    const struct i2c_dt_spec i2c;
};

struct lsm6ds3_data
{
    uint16_t accel[3];
    uint16_t gyro[3];
};

static int lsm6ds3_sample_fetch(
    const struct device *dev,
    enum sensor_channel channel)
{
    return -ENOTSUP;
}

static int lsm6ds3_channel_get(
    const struct device *dev,
    enum sensor_channel channel,
    struct sensor_value *value)
{
    return -ENOTSUP;
}

static int lsm6ds3_init(const struct device *dev)
{
    return 0;
}

static const struct sensor_driver_api lsm6ds3_api = {
    .sample_fetch = lsm6ds3_sample_fetch,
    .channel_get = lsm6ds3_channel_get,
};

#define LSM6DS3_DRIVER_INIT(n)                                \
    static struct lsm6ds3_data lsm6ds3_data_##n;              \
                                                              \
    static const struct lsm6ds3_config lsm6ds3_config_##n = { \
        .i2c = I2C_DT_SPEC_INST_GET(n),                       \
    };                                                        \
                                                              \
    SENSOR_DEVICE_DT_INST_DEFINE(n,                           \
                                 &lsm6ds3_init,               \
                                 NULL,                        \
                                 &lsm6ds3_data_##n,           \
                                 &lsm6ds3_config_##n,         \
                                 POST_KERNEL,                 \
                                 CONFIG_SENSOR_INIT_PRIORITY, \
                                 &lsm6ds3_api);

DT_INST_FOREACH_STATUS_OKAY(LSM6DS3_DRIVER_INIT)
