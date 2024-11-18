#include <zephyr/kernel.h>
#include <zephyr/device.h>

/* Placeholders to make sure our device tree is working. */

static const struct device *rf_uart_dev = DEVICE_DT_GET(DT_CHOSEN(combat_rf_uart));
static const struct device *esc_uart_dev = DEVICE_DT_GET(DT_CHOSEN(combat_esc_uart));
static const struct device *i2c_dev = DEVICE_DT_GET(DT_CHOSEN(combat_i2c));
static const struct device *spi_flash = DEVICE_DT_GET(DT_CHOSEN(combat_spi_flash));

int main(void)
{
    __ASSERT(device_is_ready(rf_uart_dev), "RF UART device not ready\n");
    __ASSERT(device_is_ready(esc_uart_dev), "ESC UART device not ready\n");
    __ASSERT(device_is_ready(i2c_dev), "I2C device not ready\n");
    __ASSERT(device_is_ready(spi_flash), "SPI flash device not ready\n");

    printk("Hello, world!\n");
    return 0;
}
