#ifndef UART_ASYNC_RINGBUF_H
#define UART_ASYNC_RINGBUF_H

#include <zephyr.h>
#include <stddef.h>
#include <stdint.h>

// Initialize UART async DMA for continuous reception
void uart_async_init(void);

// Poll data from UART ring buffer; returns number of bytes read
size_t poll_uart_ring_buffer(uint8_t *dest, size_t max_len);

// Thread entry points
void dma_thread(void *a, void *b, void *c);
void poll_thread(void *a, void *b, void *c);

#endif /* UART_ASYNC_RINGBUF_H */
