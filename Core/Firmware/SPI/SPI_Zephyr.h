/*
 * SPI_Zephyr.h - Zephyr SPI Driver Header
 *
 * This header provides the interface for the Zephyr SPI driver.
 */

#ifndef SPI_ZEPHYR_H
#define SPI_ZEPHYR_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the SPI driver
 * Must be called before using any other SPI functions.
 *
 * @return 0 on success, negative errno on failure
 *         -ENODEV if SPI device is not ready
 */
int spi_init(void);

/**
 * Send data over SPI (write-only transfer)
 *
 * @param data Pointer to data buffer to transmit
 * @param len Length of data to send in bytes
 * @return 0 on success, negative errno on failure
 *         -EINVAL if parameters are invalid
 *         -EIO if SPI write fails
 */
int spi_send_data(const uint8_t *data, size_t len);

/**
 * Receive data over SPI (read-only transfer)
 *
 * @param data Pointer to buffer to store received data
 * @param len Length of data to receive in bytes
 * @return 0 on success, negative errno on failure
 *         -EINVAL if parameters are invalid
 *         -EIO if SPI read fails
 */
int spi_receive_data(uint8_t *data, size_t len);

/**
 * Full-duplex SPI transfer (send and receive simultaneously)
 * This is the most common SPI operation mode.
 *
 * @param tx_data Pointer to transmit buffer
 * @param rx_data Pointer to receive buffer
 * @param len Length of data to transfer in bytes
 * @return 0 on success, negative errno on failure
 *         -EINVAL if parameters are invalid
 *         -EIO if SPI transceive fails
 */
int spi_transceive_data(const uint8_t *tx_data, uint8_t *rx_data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SPI_ZEPHYR_H */
