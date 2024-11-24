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
    const uint8_t colours[9][3] = {
        {100, 0, 0},
        {100, 50, 0},
        {100, 100, 0},
        {0, 100, 0},
        {0, 0, 100},
        {50, 0, 100},
        {100, 0, 80},
        {100, 100, 100},
        {0, 0, 0}};

    k_timeout_t delay = K_MSEC(500);

    printk("Testing RGB device %s\n", rgb_dev->name);

    if (!device_is_ready(rgb_dev))
    {
        printk("RGB device not ready\n");
        return -1;
    }

    for (unsigned i = 0; i < ARRAY_SIZE(colours); i++)
    {
        led_set_brightness(rgb_dev, 0, colours[i][0]);
        led_set_brightness(rgb_dev, 1, colours[i][1]);
        led_set_brightness(rgb_dev, 2, colours[i][2]);
        k_sleep(delay);
    }

    return 0;
}

static int test_flash(const struct device *flash_dev)
{
    const uint8_t expected[] = {0xdc, 0x00, 0x11, 0x22, 0x33, 0x44};
    int rc;
    uint8_t buf[sizeof(expected)];

    printk("Testing flash device %s\n", flash_dev->name);

    if (!device_is_ready(flash_dev))
    {
        printk("Flash device not ready\n");
        return -1;
    }

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

    if (!device_is_ready(elrs_radio))
    {
        printk("ELRS radio device not ready\n");
    }

    if (!device_is_ready(esc_uart_dev))
    {
        printk("ESC UART device not ready\n");
    }

    if (!device_is_ready(imu))
    {
        printk("IMU not ready\n");
    }

    printk("Hello, world!\n");

    test_rgb(rgb);
    test_flash(spi_flash);

    return 0;
}
