/*
 * SPI_Zephyr.c - Zephyr SPI Driver Implementation
 *
 * This driver provides a hardware abstraction layer for SPI communication
 * using Zephyr RTOS APIs.
 */

#include "SPI_Zephyr.h"
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <errno.h>

LOG_MODULE_REGISTER(spi_driver, LOG_LEVEL_INF);

/* Configuration: Update these to match your devicetree */
#define SPI1_NODE DT_NODELABEL(spi1)

/*
 * IMPORTANT: Update 'your_spi_device' to match your actual SPI device node label
 * from your devicetree (.dts or .overlay file)
 */
#define SPI_DEVICE_NODE DT_NODELABEL(your_spi_device)

static const struct device *spi_dev;

/* CS (Chip Select) control configuration */
static struct spi_cs_control spi_cs = {
    .gpio = GPIO_DT_SPEC_GET(SPI_DEVICE_NODE, cs_gpios),
    .delay = 0,
};

static struct spi_config spi_cfg = {
    .frequency = 1000000U,
    .operation = SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8),
    .slave = 0,
    .cs = &spi_cs,
};

/**
 * Initialize the SPI driver
 * @return 0 on success, negative errno on failure
 */
int spi_init(void) {
    spi_dev = DEVICE_DT_GET(SPI1_NODE);

    if (!device_is_ready(spi_dev)) {
        LOG_ERR("SPI device not ready");
        return -ENODEV;
    }

    // Check if CS GPIO is ready
    if (spi_cs.gpio.port && !device_is_ready(spi_cs.gpio.port)) {
        LOG_ERR("SPI CS GPIO not ready");
        return -ENODEV;
    }

    LOG_INF("SPI initialized successfully");
    return 0;
}

/**
 * Send data over SPI
 * @param data Pointer to data buffer
 * @param len Length of data to send
 * @return 0 on success, negative errno on failure
 */
int spi_send_data(const uint8_t *data, size_t len) {
    if (!spi_dev || !data || len == 0) {
        return -EINVAL;
    }

    struct spi_buf tx_buf = {
        .buf = (void *)data,
        .len = len
    };
    struct spi_buf_set tx = {
        .buffers = &tx_buf,
        .count = 1
    };

    int ret = spi_write(spi_dev, &spi_cfg, &tx);
    if (ret < 0) {
        LOG_ERR("SPI write failed: %d", ret);
        return ret;
    }

    return 0;
}

/**
 * Receive data over SPI
 * @param data Pointer to receive buffer
 * @param len Length of data to receive
 * @return 0 on success, negative errno on failure
 */
int spi_receive_data(uint8_t *data, size_t len) {
    if (!spi_dev || !data || len == 0) {
        return -EINVAL;
    }

    struct spi_buf rx_buf = {
        .buf = data,
        .len = len
    };
    struct spi_buf_set rx = {
        .buffers = &rx_buf,
        .count = 1
    };

    int ret = spi_read(spi_dev, &spi_cfg, &rx);
    if (ret < 0) {
        LOG_ERR("SPI read failed: %d", ret);
        return ret;
    }

    return 0;
}

/**
 * Full-duplex SPI transfer (send and receive simultaneously)
 * @param tx_data Pointer to transmit buffer
 * @param rx_data Pointer to receive buffer
 * @param len Length of data to transfer
 * @return 0 on success, negative errno on failure
 */
int spi_transceive_data(const uint8_t *tx_data, uint8_t *rx_data, size_t len) {
    if (!spi_dev || !tx_data || !rx_data || len == 0) {
        return -EINVAL;
    }

    struct spi_buf tx_buf = {
        .buf = (void *)tx_data,
        .len = len
    };
    struct spi_buf_set tx = {
        .buffers = &tx_buf,
        .count = 1
    };

    struct spi_buf rx_buf = {
        .buf = rx_data,
        .len = len
    };
    struct spi_buf_set rx = {
        .buffers = &rx_buf,
        .count = 1
    };

    int ret = spi_transceive(spi_dev, &spi_cfg, &tx, &rx);
    if (ret < 0) {
        LOG_ERR("SPI transceive failed: %d", ret);
        return ret;
    }

    return 0;
}
