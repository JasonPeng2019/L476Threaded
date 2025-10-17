#include <zephyr.h>
#include <device.h>
#include <drivers/uart.h>
#include <sys/ring_buffer.h>
#include <sys/printk.h>

// Include wrappers
#include "zrtos.h"

#define UART_DEVICE_NAME DT_LABEL(DT_NODELABEL(uart0))
#define RING_BUF_SIZE 1024
#define RX_BUF_SIZE 64
#define PIPE_BUF_SIZE 256

RING_BUF_DECLARE(uart_ringbuf, RING_BUF_SIZE);
static uint8_t rx_buf[RX_BUF_SIZE];
static const struct device *uart_dev;

#define THREAD_STACK_SIZE 1024
#define DMA_THREAD_PRIORITY 5
#define POLL_THREAD_PRIORITY 6

K_THREAD_STACK_DEFINE(dma_thread_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(poll_thread_stack, THREAD_STACK_SIZE);

static struct z_thread_t dma_thread_data;
static struct z_thread_t poll_thread_data;

static struct z_sem_t z_tx_done_sem;

// Allocate a buffer for the pipe, aligned to 4 bytes
unsigned char __aligned(4) pipe_buffer[PIPE_SIZE];

static struct z_pipe_t uart_pipe;

const struct device *console_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
int data_ready = false;

static void uart_callback(const struct device *dev, struct uart_event *evt, void *user_data)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(user_data);

    switch (evt->type) {
    case UART_RX_RDY: {
        data_ready = true;

        /* we might need another wrapper function to make sure the pipe's buffer doesn't get filled

        size_t len = evt->data.rx.len;
        size_t space = ring_buf_space_get(&uart_ringbuf);

        // Overwrite oldest data if not enough space
        if (len > space) {
            ring_buf_get(&uart_ringbuf, NULL, len - space);
        }
        */

        size_t bytes_written;
        // Non-blocking put; handle overflow as needed
        pipe_put(&uart_pipe, &rx_buf[evt->data.rx.offset], len,
                   &bytes_written, K_NO_WAIT);

        break;
    }
    case UART_RX_DISABLED:
        uart_rx_enable(uart_dev, rx_buf, RX_BUF_SIZE, 50);
        break;
    case UART_RX_BUF_REQUEST:
        uart_rx_buf_rsp(uart_dev, rx_buf, RX_BUF_SIZE);
        break;
    case UART_TX_DONE:
        sem_give(&z_tx_done_sem);
        break;
    case UART_TX_ABORTED:
        sem_give(&z_tx_done_sem);
        break;
    default:
        break;
    }
}

static void uart_async_init(void)
{
    uart_dev = device_get_binding(UART_DEVICE_NAME);
    if (!uart_dev) {
        printk("UART device not found\n");
        return;
    }

    sem_init(&z_tx_done_sem, 0, 1);
    pipe_init(&uart_pipe, pipe_buffer, PIPE_BUF_SIZE);

    uart_callback_set(uart_dev, uart_callback, NULL);
    uart_rx_enable(uart_dev, rx_buf, RX_BUF_SIZE, 50);
}
 
size_t poll_uart_ring_buffer(uint8_t *dest, size_t max_len)
{
    return ring_buf_get(&uart_ringbuf, dest, max_len);
}

void dma_thread(void *a, void *b, void *c)
{
    uart_async_init();
    while (1) {
        k_sleep(K_MSEC(1000));
    }
}

void poll_thread(void *a, void *b, void *c)
{
    uint8_t buffer[128];
    size_t bytes_written;
    size_t len;

    while (1) {
        k_sleep(K_MSEC(500));
        len = pipe_get(&uart_pipe, buffer, sizeof(buffer), &bytes_written, K_NO_WAIT);
        if (len > 0) {
            printk("Read %u bytes: ", len);
            for (size_t i = 0; i < len; i++) {
                printk("%c", buffer[i]);
                uart_async_send_data(&buffer[i], 1);
            }
            printk("\n");
        }
    }
}

int uart_async_send_data(const uint8_t *data, size_t len) {
    if (!uart_dev) {
        printk("UART not initialized\n");
        return -ENODEV;
    }

    int ret = uart_tx(uart_dev, data, len, SYS_FOREVER_MS);
    if (ret) {
        printk("uart_tx failed: %d\n", ret);
        return ret;
    }
    sem_take(&z_tx_done_sem, K_FOREVER);
    return 0;
}

void main(void)
{
    printk("Starting async UART DMA example\n");

    thread_start(&dma_thread_data, dma_thread_stack, THREAD_STACK_SIZE,
                    dma_thread, NULL, NULL, NULL,
                    DMA_THREAD_PRIORITY, 0, K_NO_WAIT);

    thread_start(&poll_thread_data, poll_thread_stack, THREAD_STACK_SIZE,
                    poll_thread, NULL, NULL, NULL,
                    POLL_THREAD_PRIORITY, 0, K_NO_WAIT);
}
