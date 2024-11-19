#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/led.h>

/* Placeholders to make sure our device tree is working. */

static const struct device *elrs_radio = DEVICE_DT_GET(DT_NODELABEL(elrs_radio));
static const struct device *esc_uart_dev = DEVICE_DT_GET(DT_CHOSEN(combat_esc_uart));
static const struct device *spi_flash = DEVICE_DT_GET(DT_NODELABEL(spi_flash));
static const struct device *imu = DEVICE_DT_GET(DT_NODELABEL(accel_gyro));
static const struct device *rgb = DEVICE_DT_GET(DT_PATH(rgb_led));

static int test_rgb(const struct device *rgb_dev)
{
    int brightness = 255;
    k_timeout_t delay = K_MSEC(250);

    printk("Testing RGB device %s\n", rgb_dev->name);

    printk("Setting RGB to red...\n");
    led_set_brightness(rgb_dev, 0, brightness);
    led_set_brightness(rgb_dev, 1, 0);
    led_set_brightness(rgb_dev, 2, 0);

    k_sleep(delay);

    printk("Setting RGB to green...\n");
    led_set_brightness(rgb_dev, 0, 0);
    led_set_brightness(rgb_dev, 1, brightness);
    led_set_brightness(rgb_dev, 2, 0);

    k_sleep(delay);

    printk("Setting RGB to blue...\n");
    led_set_brightness(rgb_dev, 0, 0);
    led_set_brightness(rgb_dev, 1, 0);
    led_set_brightness(rgb_dev, 2, brightness);

    k_sleep(delay);

    led_set_brightness(rgb_dev, 0, 0);
    led_set_brightness(rgb_dev, 1, 0);
    led_set_brightness(rgb_dev, 2, 0);

    return 0;
}

static int test_flash(const struct device *flash_dev)
{
    const uint8_t expected[] = {0xdc, 0x00, 0x11, 0x22, 0x33, 0x44};
    int rc;
    uint8_t buf[sizeof(expected)];

    printk("Testing flash device %s\n", flash_dev->name);

    printk("Erasing flash...\n");
    rc = flash_erase(flash_dev, 0, 4096);
    if (rc != 0)
    {
        printk("Flash erase failed! %d\n", rc);
        return rc;
    }

    printk("Writing flash...\n");
    rc = flash_write(flash_dev, 0, expected, sizeof(expected));
    if (rc != 0)
    {
        printk("Flash write failed! %d\n", rc);
        return rc;
    }

    printk("Reading flash...\n");
    rc = flash_read(flash_dev, 0, buf, sizeof(expected));
    if (rc != 0)
    {
        printk("Flash read failed! %d\n", rc);
        return rc;
    }

    printk("Comparing flash...\n");
    for (size_t i = 0; i < sizeof(expected); i++)
    {
        bool match = expected[i] == buf[i];

        printk(
            "[%d] wrote %02x, read %02x (%s)\n",
            i,
            expected[i],
            buf[i],
            match ? "match" : "mismatch");
    }

    return 0;
}

int main(void)
{
    __ASSERT(device_is_ready(elrs_radio), "ELRS radio device not ready\n");
    __ASSERT(device_is_ready(esc_uart_dev), "ESC UART device not ready\n");
    __ASSERT(device_is_ready(spi_flash), "SPI flash device not ready\n");
    __ASSERT(device_is_ready(imu), "IMU not ready\n");

    test_rgb(rgb);
    test_flash(spi_flash);

    printk("Hello, world!\n");
    return 0;
}
