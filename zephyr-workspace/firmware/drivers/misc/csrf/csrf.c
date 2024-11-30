#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/ring_buffer.h>

#include <drivers/misc/csrf.h>

#define DT_DRV_COMPAT dan_csrf

LOG_MODULE_REGISTER(dan_csrf, CONFIG_DAN_CSRF_LOG_LEVEL);

enum rx_state
{
    RX_STATE_IDLE,
    RX_STATE_GET_LEN,
    RX_STATE_GET_TYPE,
    RX_STATE_CHECK_CRC
};

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

        enum rx_state state;
        uint8_t len;
        uint8_t type;
        uint8_t payload[64];
    } rx;
};

static uint8_t crc8tab[256] = {
    0x00, 0xD5, 0x7F, 0xAA, 0xFE, 0x2B, 0x81, 0x54, 0x29, 0xFC, 0x56, 0x83, 0xD7, 0x02, 0xA8, 0x7D,
    0x52, 0x87, 0x2D, 0xF8, 0xAC, 0x79, 0xD3, 0x06, 0x7B, 0xAE, 0x04, 0xD1, 0x85, 0x50, 0xFA, 0x2F,
    0xA4, 0x71, 0xDB, 0x0E, 0x5A, 0x8F, 0x25, 0xF0, 0x8D, 0x58, 0xF2, 0x27, 0x73, 0xA6, 0x0C, 0xD9,
    0xF6, 0x23, 0x89, 0x5C, 0x08, 0xDD, 0x77, 0xA2, 0xDF, 0x0A, 0xA0, 0x75, 0x21, 0xF4, 0x5E, 0x8B,
    0x9D, 0x48, 0xE2, 0x37, 0x63, 0xB6, 0x1C, 0xC9, 0xB4, 0x61, 0xCB, 0x1E, 0x4A, 0x9F, 0x35, 0xE0,
    0xCF, 0x1A, 0xB0, 0x65, 0x31, 0xE4, 0x4E, 0x9B, 0xE6, 0x33, 0x99, 0x4C, 0x18, 0xCD, 0x67, 0xB2,
    0x39, 0xEC, 0x46, 0x93, 0xC7, 0x12, 0xB8, 0x6D, 0x10, 0xC5, 0x6F, 0xBA, 0xEE, 0x3B, 0x91, 0x44,
    0x6B, 0xBE, 0x14, 0xC1, 0x95, 0x40, 0xEA, 0x3F, 0x42, 0x97, 0x3D, 0xE8, 0xBC, 0x69, 0xC3, 0x16,
    0xEF, 0x3A, 0x90, 0x45, 0x11, 0xC4, 0x6E, 0xBB, 0xC6, 0x13, 0xB9, 0x6C, 0x38, 0xED, 0x47, 0x92,
    0xBD, 0x68, 0xC2, 0x17, 0x43, 0x96, 0x3C, 0xE9, 0x94, 0x41, 0xEB, 0x3E, 0x6A, 0xBF, 0x15, 0xC0,
    0x4B, 0x9E, 0x34, 0xE1, 0xB5, 0x60, 0xCA, 0x1F, 0x62, 0xB7, 0x1D, 0xC8, 0x9C, 0x49, 0xE3, 0x36,
    0x19, 0xCC, 0x66, 0xB3, 0xE7, 0x32, 0x98, 0x4D, 0x30, 0xE5, 0x4F, 0x9A, 0xCE, 0x1B, 0xB1, 0x64,
    0x72, 0xA7, 0x0D, 0xD8, 0x8C, 0x59, 0xF3, 0x26, 0x5B, 0x8E, 0x24, 0xF1, 0xA5, 0x70, 0xDA, 0x0F,
    0x20, 0xF5, 0x5F, 0x8A, 0xDE, 0x0B, 0xA1, 0x74, 0x09, 0xDC, 0x76, 0xA3, 0xF7, 0x22, 0x88, 0x5D,
    0xD6, 0x03, 0xA9, 0x7C, 0x28, 0xFD, 0x57, 0x82, 0xFF, 0x2A, 0x80, 0x55, 0x01, 0xD4, 0x7E, 0xAB,
    0x84, 0x51, 0xFB, 0x2E, 0x7A, 0xAF, 0x05, 0xD0, 0xAD, 0x78, 0xD2, 0x07, 0x53, 0x86, 0x2C, 0xF9};

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

static uint8_t csrf_crc8(const uint8_t *ptr, uint8_t len)
{
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++)
        crc = crc8tab[crc ^ *ptr++];
    return crc;
}

static void process_frame(struct csrf_data *data)
{
    uint32_t len;
    uint8_t buf[64];

    switch (data->rx.state)
    {
    case RX_STATE_IDLE:
        len = ring_buf_get(&data->rx.buf, buf, 1);
        if (len != 1)
            return;

        /* TODO handle other sync bytes */
        if (buf[0] != 0xc8)
            break;

        LOG_INF("sync");
        data->rx.state = RX_STATE_GET_LEN;
        break;

    case RX_STATE_GET_LEN:
        len = ring_buf_peek(&data->rx.buf, buf, 1);
        if (len != 1)
            return;

        if (buf[0] < 2 || buf[0] > 62)
        {
            ring_buf_get(&data->rx.buf, NULL, 1);
            data->rx.state = RX_STATE_IDLE;
        }

        data->rx.len = buf[0];
        data->rx.state = RX_STATE_GET_TYPE;
        LOG_INF("len=%u", data->rx.len);
        break;

    case RX_STATE_GET_TYPE:
        /* Wait until we have a complete frame worth. */
        len = ring_buf_peek(&data->rx.buf, buf, data->rx.len + 1);
        if (len != data->rx.len + 1)
        {
            LOG_INF(".");
            return;
        }

        /* TODO make sure it's a valid type. */
        data->rx.type = buf[1];
        data->rx.state = RX_STATE_CHECK_CRC;
        LOG_INF("type=%u", data->rx.type);
        break;

    case RX_STATE_CHECK_CRC:
    {
        uint8_t expected, received;

        /* At this point, we're already figured out we have a complete frame.
         * if we don't, we'll reset the state! */
        len = ring_buf_peek(&data->rx.buf, buf, data->rx.len + 1);
        if (len != data->rx.len + 1)
        {
            data->rx.state = RX_STATE_IDLE;
            return;
        }

        LOG_HEXDUMP_INF(buf, data->rx.len + 2, "frame");
        LOG_HEXDUMP_INF(&buf[2], data->rx.len - 2, "payload");

        expected = csrf_crc8(&buf[1], data->rx.len - 1);
        received = buf[data->rx.len];

        if (expected != received)
        {
            LOG_WRN("crc mismatch (exp=%02x, rx=%02x)", expected, received);
            data->rx.state = RX_STATE_IDLE;
            return;
        }

        /* We have a valid frame! */
        memcpy(data->rx.payload, &buf[2], data->rx.len - 2);
        ring_buf_get(&data->rx.buf, NULL, data->rx.len + 1);

        LOG_INF("RX (type=%d)", data->rx.type);
        LOG_HEXDUMP_INF(data->rx.payload, data->rx.len - 2, "");

        data->rx.state = RX_STATE_IDLE;
        break;
    }

    default:
        LOG_WRN("Invalid state %d", data->rx.state);
        data->rx.state = RX_STATE_IDLE;
        break;
    }
}

static void csrf_thread(void *p1, void *p2, void *p3)
{
    struct csrf_data *data = (struct csrf_data *)p1;
    int rc;

    data->rx.state = RX_STATE_IDLE;

    LOG_INF("CSRF thread started");

    while (1)
    {
        rc = k_sem_take(&data->rx.have_rx_data, K_FOREVER);
        if (rc)
            continue;

        while (!ring_buf_is_empty(&data->rx.buf))
            process_frame(data);
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
        csrf_thread,
        data,
        NULL,
        NULL,
        K_PRIO_COOP(CONFIG_DAN_CSRF_THREAD_PRIORITY),
        0,
        K_NO_WAIT);

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
