/*
 * SPI_Zephyr.h - Zephyr SPI Driver Header (Thread-Safe, Non-Blocking)
 *
 * This header provides a thread-safe, non-blocking interface for SPI using Zephyr RTOS.
 * Uses wrapper includes from nordic/src/includes for consistency.
 *
 * Features:
 * - Thread-safe operations with mutex protection
 * - Non-blocking async transfers with callbacks
 * - Optional blocking API with timeout
 * - Work queue for deferred processing
 */

#ifndef SPI_ZEPHYR_H
#define SPI_ZEPHYR_H

#include "../../../../../nordic/src/includes/zrtos.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration */
typedef struct spi_transfer_req spi_transfer_req_t;

/**
 * SPI transfer type
 */
typedef enum {
    SPI_TRANSFER_WRITE,      /* Write-only transfer */
    SPI_TRANSFER_READ,       /* Read-only transfer */
    SPI_TRANSFER_TRANSCEIVE  /* Full-duplex transfer */
} spi_transfer_type_t;

/**
 * SPI transfer callback
 * Called when async transfer completes
 *
 * @param status 0 on success, negative errno on failure
 * @param user_data User-provided context pointer
 */
typedef void (*spi_callback_t)(int status, void *user_data);

/**
 * SPI transfer request structure
 * NOTE: For async operations, caller MUST allocate this structure
 * and ensure it remains valid until the callback is invoked.
 */
struct spi_transfer_req {
    spi_transfer_type_t type;       /* Transfer type */
    const uint8_t *tx_data;         /* TX buffer (NULL for read-only) */
    uint8_t *rx_data;               /* RX buffer (NULL for write-only) */
    size_t len;                     /* Transfer length in bytes */
    spi_callback_t callback;        /* Completion callback (NULL for sync) */
    void *user_data;                /* User context for callback */

    /* Internal fields - do not modify */
    z_sem_t completion_sem;         /* For blocking operations */
    int result;                     /* Transfer result */
};

/**
 * Initialize the SPI driver (thread-safe)
 * Must be called before using any other SPI functions.
 * Creates mutex, work queue, and initializes hardware.
 *
 * @return 0 on success, negative errno on failure
 *         -ENODEV if SPI device is not ready
 *         -ENOMEM if resource allocation fails
 */
int spi_init(void);

/**
 * Shutdown the SPI driver and release resources
 * Thread-safe cleanup of mutex, work queue, etc.
 *
 * @return 0 on success, negative errno on failure
 */
int spi_shutdown(void);

/* ========== Asynchronous (Non-Blocking) API ========== */

/**
 * Submit an async SPI transfer request (non-blocking)
 * Returns immediately; callback invoked upon completion.
 * Thread-safe: multiple threads can submit requests concurrently.
 *
 * @param req Pointer to transfer request structure (must remain valid until callback)
 * @return 0 on success (queued), negative errno on failure
 *         -EINVAL if parameters are invalid
 *         -EBUSY if queue is full
 */
int spi_transfer_async(spi_transfer_req_t *req);

/**
 * Async write-only transfer (convenience wrapper)
 *
 * @param data Pointer to data buffer to transmit
 * @param len Length of data to send in bytes
 * @param callback Completion callback (called from work queue context)
 * @param user_data User context pointer passed to callback
 * @return 0 on success, negative errno on failure
 */
int spi_write_async(const uint8_t *data, size_t len,
                    spi_callback_t callback, void *user_data);

/**
 * Async read-only transfer (convenience wrapper)
 *
 * @param data Pointer to buffer to store received data
 * @param len Length of data to receive in bytes
 * @param callback Completion callback
 * @param user_data User context pointer
 * @return 0 on success, negative errno on failure
 */
int spi_read_async(uint8_t *data, size_t len,
                   spi_callback_t callback, void *user_data);

/**
 * Async full-duplex transfer (convenience wrapper)
 *
 * @param tx_data Pointer to transmit buffer
 * @param rx_data Pointer to receive buffer
 * @param len Length of data to transfer in bytes
 * @param callback Completion callback
 * @param user_data User context pointer
 * @return 0 on success, negative errno on failure
 */
int spi_transceive_async(const uint8_t *tx_data, uint8_t *rx_data, size_t len,
                         spi_callback_t callback, void *user_data);

/* ========== Synchronous (Blocking with Timeout) API ========== */

/**
 * Blocking SPI write with timeout (thread-safe)
 * Blocks until transfer completes or timeout expires.
 *
 * @param data Pointer to data buffer to transmit
 * @param len Length of data to send in bytes
 * @param timeout_ms Timeout in milliseconds (-1 for infinite)
 * @return 0 on success, negative errno on failure
 *         -ETIMEDOUT if timeout expires
 */
int spi_write(const uint8_t *data, size_t len, int32_t timeout_ms);

/**
 * Blocking SPI read with timeout (thread-safe)
 *
 * @param data Pointer to buffer to store received data
 * @param len Length of data to receive in bytes
 * @param timeout_ms Timeout in milliseconds (-1 for infinite)
 * @return 0 on success, negative errno on failure
 *         -ETIMEDOUT if timeout expires
 */
int spi_read(uint8_t *data, size_t len, int32_t timeout_ms);

/**
 * Blocking full-duplex transfer with timeout (thread-safe)
 *
 * @param tx_data Pointer to transmit buffer
 * @param rx_data Pointer to receive buffer
 * @param len Length of data to transfer in bytes
 * @param timeout_ms Timeout in milliseconds (-1 for infinite)
 * @return 0 on success, negative errno on failure
 *         -ETIMEDOUT if timeout expires
 */
int spi_transceive(const uint8_t *tx_data, uint8_t *rx_data, size_t len,
                   int32_t timeout_ms);

/* ========== Legacy API (Deprecated - use blocking API with timeout) ========== */

/**
 * Send data over SPI (blocking, infinite timeout)
 * DEPRECATED: Use spi_write() with explicit timeout instead
 *
 * @param data Pointer to data buffer to transmit
 * @param len Length of data to send in bytes
 * @return 0 on success, negative errno on failure
 */
int spi_send_data(const uint8_t *data, size_t len);

/**
 * Receive data over SPI (blocking, infinite timeout)
 * DEPRECATED: Use spi_read() with explicit timeout instead
 *
 * @param data Pointer to buffer to store received data
 * @param len Length of data to receive in bytes
 * @return 0 on success, negative errno on failure
 */
int spi_receive_data(uint8_t *data, size_t len);

/**
 * Full-duplex SPI transfer (blocking, infinite timeout)
 * DEPRECATED: Use spi_transceive() with explicit timeout instead
 *
 * @param tx_data Pointer to transmit buffer
 * @param rx_data Pointer to receive buffer
 * @param len Length of data to transfer in bytes
 * @return 0 on success, negative errno on failure
 */
int spi_transceive_data(const uint8_t *tx_data, uint8_t *rx_data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SPI_ZEPHYR_H */
