#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/byteorder.h>

#include <sensor/st/stmemsc/stmemsc.h>
#include "lsm6ds3tr-c_reg.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lsm6ds3, CONFIG_SENSOR_LOG_LEVEL);

#define DT_DRV_COMPAT st_lsm6ds3

#if DT_ANY_INST_ON_BUS_STATUS_OKAY(spi)
#include <zephyr/drivers/spi.h>
#endif

#if DT_ANY_INST_ON_BUS_STATUS_OKAY(i2c)
#include <zephyr/drivers/i2c.h>
#endif

struct lsm6ds3_config
{
    stmdev_ctx_t ctx;
    union
    {
#if DT_ANY_INST_ON_BUS_STATUS_OKAY(i2c)
        const struct i2c_dt_spec i2c;
#endif
#if DT_ANY_INST_ON_BUS_STATUS_OKAY(spi)
        const struct spi_dt_spec spi;
#endif
    } stmemsc_cfg;
};

struct lsm6ds3_data
{
    uint16_t accel[3];
    uint16_t gyro[3];
};

static int lsm6ds3_init(const struct device *dev)
{
    const struct lsm6ds3_config *cfg = dev->config;
    stmdev_ctx_t *ctx = (stmdev_ctx_t *)&cfg->ctx;
    uint8_t chip_id;

    if (lsm6ds3tr_c_device_id_get(ctx, &chip_id) < 0)
    {
        LOG_DBG("Failed reading chip id");
        return -EIO;
    }

    LOG_INF("chip id 0x%x", chip_id);

    if (chip_id != LSM6DS3TR_C_ID)
    {
        LOG_DBG("Invalid chip id 0x%x", chip_id);
        return -EIO;
    }

    return 0;
}

static const struct sensor_driver_api lsm6ds3_api = {};

#define LSM6DS3_SPI_OP (SPI_WORD_SET(8) |    \
                        SPI_OP_MODE_MASTER | \
                        SPI_MODE_CPOL |      \
                        SPI_MODE_CPHA)

#define LSM6DS3_CFG_SPI(n)                                     \
    {                                                          \
        STMEMSC_CTX_SPI(&lsm6ds3_config_##n.stmemsc_cfg),      \
        .stmemsc_cfg = {                                       \
            .spi = SPI_DT_SPEC_INST_GET(n, LSM6DS3_SPI_OP, 0), \
        },                                                     \
    }

#define LSM6DS3_CFG_I2C(n)                                \
    {                                                     \
        STMEMSC_CTX_I2C(&lsm6ds3_config_##n.stmemsc_cfg), \
        .stmemsc_cfg = {                                  \
            .i2c = I2C_DT_SPEC_INST_GET(n),               \
        },                                                \
    }

#define LSM6DS3_DRIVER_INIT(n)                                \
    static struct lsm6ds3_data lsm6ds3_data_##n;              \
                                                              \
    static const struct lsm6ds3_config lsm6ds3_config_##n =   \
        COND_CODE_1(DT_INST_ON_BUS(n, spi),                   \
                    (LSM6DS3_CFG_SPI(n)),                     \
                    (LSM6DS3_CFG_I2C(n)));                    \
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
