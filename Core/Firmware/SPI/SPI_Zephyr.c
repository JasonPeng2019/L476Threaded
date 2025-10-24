/*
 * SPI_Zephyr.c - Thread-Safe, Non-Blocking SPI Driver Implementation
 *
 * This driver provides a thread-safe hardware abstraction layer for SPI communication
 * using Zephyr RTOS APIs and wrapper functions from nordic/src/includes/zrtos.h.
 *
 * ============================================================================
 * THREAD-SAFETY DESIGN:
 * ============================================================================
 *
 * 1. BUS MUTEX PROTECTION (z_mutex_t bus_mutex):
 *    - Ensures exclusive access to SPI hardware during transfers
 *    - Prevents concurrent transfers from corrupting data
 *    - Locked in work_handler before spi_do_transfer(), released after
 *
 * 2. MESSAGE QUEUE (z_msgq_t request_queue):
 *    - Thread-safe queue for pending transfer requests
 *    - Multiple threads can submit requests concurrently via msgq_put()
 *    - Work queue processes requests sequentially via msgq_get()
 *    - Size: SPI_MAX_PENDING_REQUESTS (8 requests)
 *
 * 3. DEDICATED WORK QUEUE (z_workq_t workq):
 *    - Separate thread processes all SPI transfers asynchronously
 *    - Calling threads never block (unless using blocking API)
 *    - Priority: SPI_WORKQ_PRIORITY (5)
 *    - Stack: SPI_WORKQ_STACK_SIZE (1024 bytes)
 *
 * 4. COMPLETION SEMAPHORE (z_sem_t completion_sem):
 *    - One per transfer request
 *    - Used by blocking API to wait for transfer completion
 *    - Given by work_handler when transfer completes
 *    - Taken by blocking functions (spi_write, spi_read, spi_transceive)
 *
 * ============================================================================
 * NON-BLOCKING OPERATION:
 * ============================================================================
 *
 * ASYNC API:
 * - spi_transfer_async(): Returns immediately after queuing request
 * - Callback invoked from work queue thread when transfer completes
 * - Caller must ensure request structure remains valid until callback
 *
 * BLOCKING API:
 * - spi_write(), spi_read(), spi_transceive(): Block with timeout
 * - Internally uses async API + semaphore wait
 * - Thread-safe: multiple threads can call simultaneously
 *
 * ============================================================================
 * CRITICAL NOTES:
 * ============================================================================
 *
 * - Do NOT use static variables in async convenience functions (race condition!)
 * - Async requests MUST remain valid until callback (use heap/static/global)
 * - All wrapper functions are from nordic/src/includes/zrtos.h
 * - Initialization MUST be called before any transfers (spi_init())
 */

#include "SPI_Zephyr.h"
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
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

/* SPI transfer request pool size */
#define SPI_MAX_PENDING_REQUESTS 8

/* Work queue configuration */
#define SPI_WORKQ_STACK_SIZE 1024
#define SPI_WORKQ_PRIORITY 5

/* ========== Private Data Structures ========== */

/* SPI driver state */
static struct {
    const struct device *dev;           /* SPI device */
    struct spi_config cfg;              /* SPI configuration */
    struct spi_cs_control cs;           /* Chip select control */

    z_mutex_t bus_mutex;                /* Bus access mutex */
    z_msgq_t request_queue;             /* Pending request queue */
    z_workq_t workq;                    /* Dedicated work queue */
    z_work_t work;                      /* Work item for processing */

    bool initialized;                   /* Initialization flag */
} spi_ctx;

/* Work queue stack */
THREAD_STACK(spi_workq_stack, SPI_WORKQ_STACK_SIZE);

/* Message queue buffer for request pointers */
static char __aligned(4) msgq_buffer[SPI_MAX_PENDING_REQUESTS * sizeof(spi_transfer_req_t *)];

/* ========== Private Function Declarations ========== */
static void spi_work_handler(struct k_work *work);
static int spi_do_transfer(spi_transfer_req_t *req);

/* ========== Initialization & Shutdown ========== */

/**
 * Initialize the SPI driver (thread-safe)
 */
int spi_init(void) {
    if (spi_ctx.initialized) {
        LOG_WRN("SPI already initialized");
        return 0;
    }

    /* Get SPI device */
    spi_ctx.dev = DEVICE_DT_GET(SPI1_NODE);
    if (!device_ready(spi_ctx.dev)) {
        LOG_ERR("SPI device not ready");
        return -ENODEV;
    }

    /* Configure CS control */
    spi_ctx.cs.gpio = GPIO_DT_SPEC_GET(SPI_DEVICE_NODE, cs_gpios);
    spi_ctx.cs.delay = 0;

    /* Check if CS GPIO is ready */
    if (spi_ctx.cs.gpio.port && !device_ready(spi_ctx.cs.gpio.port)) {
        LOG_ERR("SPI CS GPIO not ready");
        return -ENODEV;
    }

    /* Configure SPI parameters */
    spi_ctx.cfg.frequency = 1000000U;  /* 1 MHz */
    spi_ctx.cfg.operation = SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8);
    spi_ctx.cfg.slave = 0;
    spi_ctx.cfg.cs = &spi_ctx.cs;

    /* Initialize synchronization primitives using wrappers */
    mutex_init(&spi_ctx.bus_mutex);
    msgq_init(&spi_ctx.request_queue, msgq_buffer,
              sizeof(spi_transfer_req_t *), SPI_MAX_PENDING_REQUESTS);

    /* Initialize work item */
    work_init(&spi_ctx.work, spi_work_handler);

    /* Start dedicated work queue */
    workq_start(&spi_ctx.workq, spi_workq_stack,
                SPI_WORKQ_STACK_SIZE, SPI_WORKQ_PRIORITY, "spi_workq");

    spi_ctx.initialized = true;

    LOG_INF("SPI initialized successfully (thread-safe, non-blocking mode)");
    return 0;
}

/**
 * Shutdown the SPI driver
 */
int spi_shutdown(void) {
    if (!spi_ctx.initialized) {
        return -ENODEV;
    }

    /* Stop work queue (drains pending work) */
    workq_stop(&spi_ctx.workq);

    /* Cancel any pending work */
    work_cancel(&spi_ctx.work);

    /* Purge pending requests and notify callbacks */
    spi_transfer_req_t *req;
    while (msgq_get(&spi_ctx.request_queue, &req, 0) == 0) {
        if (req && req->callback) {
            req->callback(-ECANCELED, req->user_data);
        }
    }

    /* Purge any remaining messages in queue */
    msgq_purge(&spi_ctx.request_queue);

    spi_ctx.initialized = false;

    LOG_INF("SPI shutdown complete");
    return 0;
}

/* ========== Core Transfer Logic ========== */

/**
 * Perform actual SPI transfer (called with mutex held)
 */
static int spi_do_transfer(spi_transfer_req_t *req) {
    int ret = 0;
    struct spi_buf_set tx_bufs = {0};
    struct spi_buf_set rx_bufs = {0};
    struct spi_buf tx_buf, rx_buf;

    /* Validate request */
    if (!req || req->len == 0) {
        return -EINVAL;
    }

    /* Prepare TX buffer if needed */
    if (req->type == SPI_TRANSFER_WRITE || req->type == SPI_TRANSFER_TRANSCEIVE) {
        if (!req->tx_data) {
            return -EINVAL;
        }
        tx_buf.buf = (void *)req->tx_data;
        tx_buf.len = req->len;
        tx_bufs.buffers = &tx_buf;
        tx_bufs.count = 1;
    }

    /* Prepare RX buffer if needed */
    if (req->type == SPI_TRANSFER_READ || req->type == SPI_TRANSFER_TRANSCEIVE) {
        if (!req->rx_data) {
            return -EINVAL;
        }
        rx_buf.buf = req->rx_data;
        rx_buf.len = req->len;
        rx_bufs.buffers = &rx_buf;
        rx_bufs.count = 1;
    }

    /* Execute transfer based on type using native Zephyr SPI driver API */
    switch (req->type) {
        case SPI_TRANSFER_WRITE:
            /* Use Zephyr's spi_write_dt or spi_write API */
            ret = spi_transceive(spi_ctx.dev, &spi_ctx.cfg, &tx_bufs, NULL);
            break;

        case SPI_TRANSFER_READ:
            /* Use Zephyr's spi_read_dt or spi_read API */
            ret = spi_transceive(spi_ctx.dev, &spi_ctx.cfg, NULL, &rx_bufs);
            break;

        case SPI_TRANSFER_TRANSCEIVE:
            /* Use Zephyr's spi_transceive API (full-duplex) */
            ret = spi_transceive(spi_ctx.dev, &spi_ctx.cfg, &tx_bufs, &rx_bufs);
            break;

        default:
            ret = -EINVAL;
            break;
    }

    if (ret < 0) {
        LOG_ERR("SPI transfer failed: type=%d, ret=%d", req->type, ret);
    }

    return ret;
}

/**
 * Work queue handler - processes SPI transfers asynchronously
 * This runs in the dedicated work queue thread, ensuring thread-safe
 * processing of all SPI transfer requests.
 */
static void spi_work_handler(struct k_work *work) {
    spi_transfer_req_t *req;

    /* Process all pending requests (thread-safe message queue) */
    while (msgq_get(&spi_ctx.request_queue, &req, 0) == 0) {
        int result;

        /* Acquire bus mutex for exclusive access (thread-safe) */
        if (mutex_lock(&spi_ctx.bus_mutex, -1) != 0) {
            LOG_ERR("Failed to acquire bus mutex");
            result = -EBUSY;
        } else {
            /* Perform transfer while holding mutex */
            result = spi_do_transfer(req);

            /* Release bus mutex */
            mutex_unlock(&spi_ctx.bus_mutex);
        }

        /* Store result for blocking callers */
        req->result = result;

        /* Notify via callback if provided */
        if (req->callback) {
            req->callback(result, req->user_data);
        }

        /* Signal completion semaphore for blocking callers (thread-safe) */
        sem_give(&req->completion_sem);
    }
}

/* ========== Asynchronous (Non-Blocking) API ========== */

/**
 * Submit async transfer request (non-blocking, thread-safe)
 *
 * IMPORTANT: The request structure must remain valid until the callback
 * is invoked or the transfer completes. For async operations, allocate
 * the request structure on the heap or as a static/global variable.
 */
int spi_transfer_async(spi_transfer_req_t *req) {
    if (!spi_ctx.initialized) {
        return -ENODEV;
    }

    if (!req) {
        return -EINVAL;
    }

    /* Initialize completion semaphore (used by blocking API) */
    sem_init(&req->completion_sem, 0, 1);
    req->result = -EINPROGRESS;

    /* Add request pointer to queue (thread-safe message queue) */
    int ret = msgq_put(&spi_ctx.request_queue, &req, 0);
    if (ret != 0) {
        LOG_ERR("Failed to queue SPI request: queue full");
        return -EBUSY;
    }

    /* Submit work to dedicated queue (thread-safe) */
    work_submit_to(&spi_ctx.workq, &spi_ctx.work);

    return 0;
}

/**
 * Async write convenience wrapper
 * NOTE: Caller MUST allocate spi_transfer_req_t and ensure it remains valid
 * until the callback is invoked. This function cannot use static storage
 * as it would cause race conditions in multi-threaded environments.
 */
int spi_write_async(const uint8_t *data, size_t len,
                    spi_callback_t callback, void *user_data) {
    /* ERROR: This convenience function requires heap allocation or caller-provided buffer.
     * Use spi_transfer_async() directly with a properly allocated request structure. */
    return -ENOTSUP;
}

/**
 * Async read convenience wrapper
 * NOTE: Caller MUST allocate spi_transfer_req_t and ensure it remains valid
 * until the callback is invoked. This function cannot use static storage
 * as it would cause race conditions in multi-threaded environments.
 */
int spi_read_async(uint8_t *data, size_t len,
                   spi_callback_t callback, void *user_data) {
    /* ERROR: This convenience function requires heap allocation or caller-provided buffer.
     * Use spi_transfer_async() directly with a properly allocated request structure. */
    return -ENOTSUP;
}

/**
 * Async transceive convenience wrapper
 * NOTE: Caller MUST allocate spi_transfer_req_t and ensure it remains valid
 * until the callback is invoked. This function cannot use static storage
 * as it would cause race conditions in multi-threaded environments.
 */
int spi_transceive_async(const uint8_t *tx_data, uint8_t *rx_data, size_t len,
                         spi_callback_t callback, void *user_data) {
    /* ERROR: This convenience function requires heap allocation or caller-provided buffer.
     * Use spi_transfer_async() directly with a properly allocated request structure. */
    return -ENOTSUP;
}

/* ========== Synchronous (Blocking) API ========== */

/**
 * Blocking write with timeout
 */
int spi_write(const uint8_t *data, size_t len, int32_t timeout_ms) {
    if (!data || len == 0) {
        return -EINVAL;
    }

    spi_transfer_req_t req = {
        .type = SPI_TRANSFER_WRITE,
        .tx_data = data,
        .rx_data = NULL,
        .len = len,
        .callback = NULL,  /* No callback for blocking mode */
        .user_data = NULL,
    };

    /* Submit async request */
    int ret = spi_transfer_async(&req);
    if (ret != 0) {
        return ret;
    }

    /* Wait for completion */
    ret = sem_take(&req.completion_sem, timeout_ms);
    if (ret != 0) {
        return -ETIMEDOUT;
    }

    return req.result;
}

/**
 * Blocking read with timeout
 */
int spi_read(uint8_t *data, size_t len, int32_t timeout_ms) {
    if (!data || len == 0) {
        return -EINVAL;
    }

    spi_transfer_req_t req = {
        .type = SPI_TRANSFER_READ,
        .tx_data = NULL,
        .rx_data = data,
        .len = len,
        .callback = NULL,
        .user_data = NULL,
    };

    int ret = spi_transfer_async(&req);
    if (ret != 0) {
        return ret;
    }

    ret = sem_take(&req.completion_sem, timeout_ms);
    if (ret != 0) {
        return -ETIMEDOUT;
    }

    return req.result;
}

/**
 * Blocking transceive with timeout
 */
int spi_transceive(const uint8_t *tx_data, uint8_t *rx_data, size_t len,
                   int32_t timeout_ms) {
    if (!tx_data || !rx_data || len == 0) {
        return -EINVAL;
    }

    spi_transfer_req_t req = {
        .type = SPI_TRANSFER_TRANSCEIVE,
        .tx_data = tx_data,
        .rx_data = rx_data,
        .len = len,
        .callback = NULL,
        .user_data = NULL,
    };

    int ret = spi_transfer_async(&req);
    if (ret != 0) {
        return ret;
    }

    ret = sem_take(&req.completion_sem, timeout_ms);
    if (ret != 0) {
        return -ETIMEDOUT;
    }

    return req.result;
}

/* ========== Legacy API (Deprecated) ========== */

/**
 * Legacy send (blocking, infinite timeout)
 */
int spi_send_data(const uint8_t *data, size_t len) {
    return spi_write(data, len, -1);
}

/**
 * Legacy receive (blocking, infinite timeout)
 */
int spi_receive_data(uint8_t *data, size_t len) {
    return spi_read(data, len, -1);
}

/**
 * Legacy transceive (blocking, infinite timeout)
 */
int spi_transceive_data(const uint8_t *tx_data, uint8_t *rx_data, size_t len) {
    return spi_transceive(tx_data, rx_data, len, -1);
}
