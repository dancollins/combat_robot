#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/ring_buffer.h>

#include <drivers/misc/csrf.h>

#define DT_DRV_COMPAT dan_csrf

LOG_MODULE_REGISTER(dan_csrf, CONFIG_DAN_CSRF_LOG_LEVEL);

struct csrf_config
{
    const struct device *uart_dev;
};

struct csrf_data
{
    const struct device *dev;

    struct k_thread thread;
    K_KERNEL_STACK_MEMBER(
        thread_stack, CONFIG_DAN_CSRF_THREAD_STACK_SIZE);

    struct
    {
        struct ring_buf buf;
        uint8_t buf_data[CONFIG_DAN_CSRF_RX_BUFFER_SIZE];
        struct k_sem have_rx_data;
    } rx;
};

static void uart_callback(const struct device *dev, void *user_data)
{
    const struct device *csrf_dev = (const struct device *)user_data;
    struct csrf_data *data = csrf_dev->data;
    uint8_t ch;
    bool have_rx_data = false;

    /* Acknowledge the interrupt, and then check if there's
     * actually work to do. */
    if ((uart_irq_update(dev) <= 0) ||
        (uart_irq_rx_ready(dev) <= 0))
        return;

    while (uart_poll_in(dev, &ch) == 0)
    {
        ring_buf_put(&data->rx.buf, &ch, 1);
        have_rx_data = true;
    }

    if (have_rx_data)
    {
        k_sem_give(&data->rx.have_rx_data);
    }
}

static void csrf_thread(struct csrf_data *data)
{
    int rc;

    LOG_INF("CSRF thread started");

    while (1)
    {
        rc = k_sem_take(&data->rx.have_rx_data, K_FOREVER);
        if (rc)
            continue;

        while (!ring_buf_is_empty(&data->rx.buf))
        {
            uint8_t ch;
            rc = ring_buf_get(&data->rx.buf, &ch, 1);
            if (rc <= 0)
                break;

            LOG_INF("rx %02x", ch);
        }
    }
}

static int csrf_init(const struct device *dev)
{
    const struct csrf_config *cfg = dev->config;
    struct csrf_data *data = dev->data;
    int rc;

    LOG_INF("Initialising %s", dev->name);

    if (!device_is_ready(cfg->uart_dev))
    {
        LOG_ERR("%s: UART not ready!", dev->name);
        return -ENODEV;
    }

    data->dev = dev;

    ring_buf_init(
        &data->rx.buf,
        CONFIG_DAN_CSRF_RX_BUFFER_SIZE,
        data->rx.buf_data);

    rc = k_sem_init(&data->rx.have_rx_data, 0, 1);
    if (rc)
    {
        LOG_ERR("%s: Failed to init semaphore", dev->name);
        return rc;
    }

    k_thread_create(
        &data->thread,
        data->thread_stack,
        CONFIG_DAN_CSRF_THREAD_STACK_SIZE,
        (k_thread_entry_t)csrf_thread,
        data,
        NULL,
        NULL,
        K_PRIO_COOP(CONFIG_DAN_CSRF_THREAD_PRIORITY),
        0,
        K_NO_WAIT);

    rc = k_thread_name_set(&data->thread, "csrf_thread");
    if (rc)
    {
        LOG_ERR("%s: Failed to set thread name", dev->name);
    }

    rc = uart_irq_callback_user_data_set(
        cfg->uart_dev, uart_callback, (void *)dev);
    if (rc)
    {
        LOG_ERR("%s: Failed to set UART callback", dev->name);
        return rc;
    }

    uart_irq_rx_enable(cfg->uart_dev);

    return 0;
}

struct csrf_driver_api csrf_api = {};

#define CSRF_DEFINE(n)                                   \
    static const struct csrf_config csrf_cfg_##n = {     \
        .uart_dev = DEVICE_DT_GET(DT_INST_BUS(n)),       \
    };                                                   \
                                                         \
    static struct csrf_data csrf_data_##n;               \
                                                         \
    DEVICE_DT_INST_DEFINE(n, csrf_init,                  \
                          NULL,                          \
                          &csrf_data_##n,                \
                          &csrf_cfg_##n,                 \
                          POST_KERNEL,                   \
                          CONFIG_DAN_CSRF_INIT_PRIORITY, \
                          &csrf_api);

DT_INST_FOREACH_STATUS_OKAY(CSRF_DEFINE)
