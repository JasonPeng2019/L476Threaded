/*
* UARTZephyr2.c
*
*  Created on: Sep 16, 2025
*      Author: buh07
*
*/

#include "UARTZephyr2.h"
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/irq.h>
#include <zephyr/kernel_structs.h>
#include <zephyr/timing/timing.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include "../../../../nordic/src/includes/zrtos.h"


LOG_MODULE_REGISTER(uartthreaded, LOG_LEVEL_INF);

// Fallback in case building outside of a Zephyr environment
#ifndef K_NO_WAIT
#define K_NO_WAIT 0
#endif
#ifndef K_FOREVER
#define K_FOREVER (-1)
#endif

// Default thread priority
#ifndef CONFIG_UARTTHREADED_THREAD_PRIORITY
#define CONFIG_UARTTHREADED_THREAD_PRIORITY 5
#endif

// UART Polling thread stack size
#define UART_POLL_STACK_SIZE 512

// Maximum number of UART instances
#define UART_REGISTRY_MAX 2
static tUART * g_uart_registry[UART_REGISTRY_MAX];
static uint32_t g_uart_registry_count = 0;
static z_mutex_t g_uart_registry_lock;
static z_mutex_t g_polling_init_lock;

static void UART_Thread_Entry(void *p1, void *p2, void *p3);

// Everything needed for polling (hopefully)
static Queue * g_uart_event_queue = NULL; // Using queuezephyr for proper thread-safe queue with peeking
static struct k_work poll_work;
static struct k_timer poll_timer;
static struct k_poll_signal g_poll_signal;
static struct k_pipe g_uart_pipes[UART_REGISTRY_MAX];
static uint8_t g_uart_pipe_buf[UART_REGISTRY_MAX][UART_RX_BUFF_SIZE];
static bool g_polling_started = false;

K_THREAD_STACK_DEFINE(uart_poll_stack, UART_POLL_STACK_SIZE);
static struct k_thread uart_poll_thread_data;
static void uart_poll_work_handler(struct k_work *work);
static void poll_timer_handler(struct k_timer *timer){ ARG_UNUSED(timer); k_work_submit(&poll_work); }
static void uart_poll_thread(void *p1, void *p2, void *p3);
static void uart_poll_timer_handler(struct k_timer *timer){ ARG_UNUSED(timer); }
static void uart_async_callback(const struct device *dev, struct uart_event *evt, void *user_data);

/* UART async callback - handles DMA completion events */
static void uart_async_callback(const struct device *dev, struct uart_event *evt, void *user_data)
{
    ARG_UNUSED(dev);
    tUART *uart = (tUART *)user_data;
    if (!uart) return;

    switch (evt->type) {
    case UART_RX_RDY:
        /* DMA has received data - copy immediately to RX_Pipe (ISR-safe) */
        {
            uint8_t *src = evt->data.rx.buf + evt->data.rx.offset;
            size_t bytes_written;

            /* k_pipe_put is ISR-safe with K_NO_WAIT */
            int rc = pipe_put(&uart->RX_Pipe, src, evt->data.rx.len,
                             &bytes_written, 1, K_NO_WAIT);

            if (rc == 0 && bytes_written == evt->data.rx.len) {
                /* Data copied successfully - set flag for 0.5s notification */
                uart->New_Data_Available = true;
            } else {
                /* Pipe full or error - data lost */
                LOG_WRN("UART RX pipe full in ISR - data lost");
            }
        }
        break;

    case UART_RX_BUF_REQUEST:
        /* DMA requesting next buffer - provide it for continuous streaming */
        {
            /* Alternate between buffers */
            static uint8_t next_buf_idx = 1;
            uart_rx_buf_rsp(dev, uart->DMA_RX_Buf[next_buf_idx], uart->DMA_RX_Buf_Size);
            next_buf_idx = (next_buf_idx + 1) % 2;
        }
        break;

    case UART_RX_BUF_RELEASED:
        /* Buffer released by DMA - no action needed (data already copied) */
        break;

    case UART_RX_DISABLED:
        /* RX disabled event */
        LOG_DBG("UART RX disabled");
        break;

    case UART_TX_DONE:
        /* TX DMA complete - signal semaphore */
        sem_give(&uart->TX_Done_Sem);
        break;

    case UART_TX_ABORTED:
        LOG_WRN("UART TX aborted");
        sem_give(&uart->TX_Done_Sem);
        break;

    default:
        break;
    }
}

static void uart_poll_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    static int token = 1;
    if (g_uart_event_queue) {
        if (!Enqueue(g_uart_event_queue, &token, sizeof(token))) {
            LOG_WRN("uart_poll_work_handler: Failed to enqueue event token");
        }
    }
    k_poll_signal_raise(&g_poll_signal, 0);
}

void Init_UART_CallBack_Queue(void){
    memset(g_uart_registry, 0, sizeof(g_uart_registry));
    g_uart_registry_count = 0;
    mutex_init(&g_uart_registry_lock);
    mutex_init(&g_polling_init_lock);
    g_uart_event_queue = Prep_Queue(); // Initialize queue using queuezephyr
    if (!g_uart_event_queue) {
        LOG_ERR("Init_UART_CallBack_Queue: Failed to initialize event queue");
        return;
    }
    k_work_init(&poll_work, uart_poll_work_handler);
    k_timer_init(&poll_timer, poll_timer_handler, NULL);
    k_poll_signal_init(&g_poll_signal);
    for (size_t i = 0; i < UART_REGISTRY_MAX; ++i) {
        k_pipe_init(&g_uart_pipes[i], g_uart_pipe_buf[i], sizeof(g_uart_pipe_buf[i]), 4);
    }

    if (mutex_lock(&g_polling_init_lock, -1) == 0) {
        if (!g_polling_started) {
            k_thread_create(&uart_poll_thread_data, uart_poll_stack, UART_POLL_STACK_SIZE,
                            uart_poll_thread, NULL, NULL, NULL,
                            K_PRIO_PREEMPT(CONFIG_UARTTHREADED_THREAD_PRIORITY), K_ESSENTIAL, K_NO_WAIT);
            k_timer_start(&poll_timer, K_MSEC(500), K_MSEC(500));
            g_polling_started = true;
        }
        mutex_unlock(&g_polling_init_lock);
    }
}

static void uart_poll_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
    struct k_poll_event kevent;
    while (1) {
        poll_event_init(&kevent, K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &g_poll_signal);
        poll(&kevent, 1, -1);
        while (g_uart_event_queue && Queue_Size(g_uart_event_queue) > 0) {
            size_t tok_size;
            void *tok = Dequeue(g_uart_event_queue, &tok_size);
            if (tok) k_free(tok); // Free the allocated token data

            if (mutex_lock(&g_uart_registry_lock, -1) == 0) {
                for (size_t i = 0; i < g_uart_registry_count; ++i) {
                    tUART *u = g_uart_registry[i];
                    if (!u) continue;

                    /* Try to lock this UART's state mutex with no wait to avoid blocking */
                    if (mutex_lock(&u->state_mutex, 0) == 0) {
                        if (u->UART_Enabled) {
                            /* DMA UARTs: check if new data arrived in last 0.5s */
                            if (u->Use_DMA && u->DMA_RX_Buf[0]) {
                                /* ISR already copied data to RX_Pipe - just check flag for notification */
                                if (u->New_Data_Available) {
                                    /* Data was received in last 0.5s interval */
                                    LOG_DBG("UART data received in last 0.5s (already in RX_Pipe)");

                                    /* Clear flag for next 0.5s interval */
                                    u->New_Data_Available = false;

                                    /* Optional: Signal application that data is available
                                     * Application can read from RX_Pipe via UART_Receive() */
                                } else {
                                    LOG_DBG("No new UART data in last 0.5s interval");
                                }
                            }
                            /* Non-DMA UARTs (SUDO): fall back to polling hardware FIFO */
                            else if (!u->Use_DMA && u->UART_Handle) {
                                uint8_t ch;
                                while (uart_poll_in(u->UART_Handle, &ch) == 0) {
                                    size_t bytes_written = 0;
                                    int rc = pipe_put(&u->RX_Pipe, &ch, 1, &bytes_written, 0);
                                    ARG_UNUSED(rc);
                                    ARG_UNUSED(bytes_written);
                                }
                            }
                        }
                        mutex_unlock(&u->state_mutex);
                    }
                }
                mutex_unlock(&g_uart_registry_lock);
            }
        }
    }
}

void Cleanup_UART_CallBack_Queue(void) {
    if (mutex_lock(&g_polling_init_lock, -1) == 0) {
        if (g_polling_started) {
            k_timer_stop(&poll_timer);
            k_thread_abort(&uart_poll_thread_data);
            g_polling_started = false;
        }
        mutex_unlock(&g_polling_init_lock);
    }

    if (g_uart_event_queue) {
        Free_Queue(g_uart_event_queue);
        g_uart_event_queue = NULL;
    }
}

static void UART_Thread_Entry(void *p1, void *p2, void *p3)
{
   ARG_UNUSED(p2);
   ARG_UNUSED(p3);
   tUART * UART = (tUART *)p1;

   while(1)
   {
       /* Wait for next TX buffer pointer from queue */
       TX_Node *node = NULL;
       msgq_get(&UART->TX_Queue, &node, -1);
       if(node == NULL)
           continue;

       UART->TX_Buffer = node;
       /* serialize state changes and protect resource lifetimes during transmit */
       if (mutex_lock(&UART->state_mutex, -1) != 0) {
           /* cannot lock, free and continue */
           k_free(UART->TX_Buffer->Data);
           k_free(UART->TX_Buffer);
           UART->TX_Buffer = NULL;
           continue;
       }

       if(!UART->UART_Enabled)
       {
           /* drop and free */
           k_free(UART->TX_Buffer->Data);
           k_free(UART->TX_Buffer);
           UART->TX_Buffer = NULL;
           mutex_unlock(&UART->state_mutex);
           continue;
       }

       UART->Currently_Transmitting = true;
       if(UART->Use_DMA && UART->UART_Handle != NULL)
       {
           int rc = uart_tx(UART->UART_Handle, UART->TX_Buffer->Data, UART->TX_Buffer->Data_Size, K_FOREVER);
           if (rc == 0) {
               /* Wait for callback to signal completion */
               sem_take(&UART->TX_Done_Sem, -1);
           }
       }
       else if (UART->SUDO_Handler != NULL)
       {
           UART->SUDO_Handler->SUDO_Transmit(UART, UART->TX_Buffer->Data, UART->TX_Buffer->Data_Size);
       }

       UART->Currently_Transmitting = false;
       mutex_unlock(&UART->state_mutex);
       /* Free buffer after transmit */
       k_free(UART->TX_Buffer->Data);
       k_free(UART->TX_Buffer);
       UART->TX_Buffer = NULL;
   }
}


tUART * Init_DMA_UART(const struct device * uart_dev)
{
   if (!uart_dev) {
       LOG_ERR("Invalid UART device");
       return NULL;
   }
   if (!device_ready(uart_dev)) {
       LOG_ERR("UART device not ready");
       return NULL;
   }

    tUART * UART = (tUART *)k_calloc(1, sizeof(tUART));
    if (UART == NULL) {
       LOG_ERR("Init_DMA_UART: Memory allocation failed");
       return NULL;
   }

    UART->UART_Handle = uart_dev;
    UART->Use_DMA = true;
    UART->UART_Enabled = true;
    UART->TX_Buffer = NULL;
    UART->Currently_Transmitting = false;
    UART->SUDO_Handler = NULL;
    mutex_init(&UART->state_mutex);
    UART->RX_Pipe_Storage = (uint8_t *)k_calloc(1, UART_RX_BUFF_SIZE);

    if (UART->RX_Pipe_Storage) {
        UART->RX_Pipe_Size = UART_RX_BUFF_SIZE;
        pipe_init(&UART->RX_Pipe, UART->RX_Pipe_Storage, UART->RX_Pipe_Size);
    } else {
        UART->RX_Pipe_Size = 0;
    }

    /* Allocate DMA RX buffers for double buffering */
    UART->DMA_RX_Buf_Size = UART_RX_BUFF_SIZE;
    UART->DMA_RX_Buf[0] = (uint8_t *)k_calloc(1, UART->DMA_RX_Buf_Size);
    UART->DMA_RX_Buf[1] = (uint8_t *)k_calloc(1, UART->DMA_RX_Buf_Size);

    /* Initialize notification flag for 0.5s polling */
    UART->New_Data_Available = false;

    if (!UART->DMA_RX_Buf[0] || !UART->DMA_RX_Buf[1]) {
        LOG_ERR("Init_DMA_UART: DMA buffer allocation failed");
        if (UART->DMA_RX_Buf[0]) k_free(UART->DMA_RX_Buf[0]);
        if (UART->DMA_RX_Buf[1]) k_free(UART->DMA_RX_Buf[1]);
        if (UART->RX_Pipe_Storage) k_free(UART->RX_Pipe_Storage);
        k_free(UART);
        return NULL;
    }

    UART->Thread_Stack_Size = sizeof(UART->Thread_Stack);

    UART->Queue_Length = 16;
    UART->Queue_Storage = k_calloc(UART->Queue_Length, sizeof(void*));
    if (UART->Queue_Storage == NULL) {
        LOG_ERR("Init_DMA_UART: Queue memory allocation failed");
        if (UART->DMA_RX_Buf[0]) k_free(UART->DMA_RX_Buf[0]);
        if (UART->DMA_RX_Buf[1]) k_free(UART->DMA_RX_Buf[1]);
        if (UART->RX_Pipe_Storage) k_free(UART->RX_Pipe_Storage);
        k_free(UART);
        return NULL;
    }

    msgq_init(&UART->TX_Queue, UART->Queue_Storage, sizeof(void*), UART->Queue_Length);
    sem_init(&UART->TX_Done_Sem, 1, 1);

    k_thread_create(&UART->Thread.thread, UART->Thread_Stack, UART->Thread_Stack_Size,
                    UART_Thread_Entry, UART, NULL, NULL,
                    K_PRIO_PREEMPT(CONFIG_UARTTHREADED_THREAD_PRIORITY), 0, K_NO_WAIT);
    UART->Thread.tid = &UART->Thread.thread;

    /* Set up async UART callback */
    int rc = uart_callback_set(uart_dev, uart_async_callback, UART);
    if (rc != 0) {
        LOG_ERR("Init_DMA_UART: uart_callback_set failed: %d", rc);
        thread_abort(&UART->Thread);
        if (UART->Queue_Storage) k_free(UART->Queue_Storage);
        if (UART->DMA_RX_Buf[0]) k_free(UART->DMA_RX_Buf[0]);
        if (UART->DMA_RX_Buf[1]) k_free(UART->DMA_RX_Buf[1]);
        if (UART->RX_Pipe_Storage) k_free(UART->RX_Pipe_Storage);
        k_free(UART);
        return NULL;
    }

    /* Enable async RX with first buffer */
    rc = uart_rx_enable(uart_dev, UART->DMA_RX_Buf[0], UART->DMA_RX_Buf_Size, SYS_FOREVER_US);
    if (rc != 0) {
        LOG_ERR("Init_DMA_UART: uart_rx_enable failed: %d", rc);
        thread_abort(&UART->Thread);
        if (UART->Queue_Storage) k_free(UART->Queue_Storage);
        if (UART->DMA_RX_Buf[0]) k_free(UART->DMA_RX_Buf[0]);
        if (UART->DMA_RX_Buf[1]) k_free(UART->DMA_RX_Buf[1]);
        if (UART->RX_Pipe_Storage) k_free(UART->RX_Pipe_Storage);
        k_free(UART);
        return NULL;
    }

    if (mutex_lock(&g_uart_registry_lock, -1) == 0) {
        if (g_uart_registry_count < UART_REGISTRY_MAX) {
            g_uart_registry[g_uart_registry_count++] = UART;
        } else {
            LOG_ERR("Init_DMA_UART: Maximum UART instances reached (post-init)");
            mutex_unlock(&g_uart_registry_lock);
            uart_rx_disable(uart_dev);
            thread_abort(&UART->Thread);
            if (UART->Queue_Storage) k_free(UART->Queue_Storage);
            if (UART->DMA_RX_Buf[0]) k_free(UART->DMA_RX_Buf[0]);
            if (UART->DMA_RX_Buf[1]) k_free(UART->DMA_RX_Buf[1]);
            if (UART->RX_Pipe_Storage) k_free(UART->RX_Pipe_Storage);
            k_free(UART);
            return NULL;
        }
        mutex_unlock(&g_uart_registry_lock);
    }

    return UART;
}

tUART * Init_SUDO_UART(void (*Transmit_Func_Ptr)(tUART*, uint8_t*, uint16_t), void (*Receive_Func_Ptr)(tUART*, uint8_t*, uint16_t*)){
    tUART * UART = (tUART*)k_calloc(1, sizeof(tUART));
    if (UART == NULL) {
        LOG_ERR("Init_SUDO_UART: malloc failed");
        return NULL;

    }

    UART->UART_Handle = NULL;
    UART->Use_DMA = false;
    UART->UART_Enabled = true;
    UART->TX_Buffer = NULL;
    UART->Currently_Transmitting = false;

    UART->SUDO_Handler = (SUDO_UART *)k_calloc(1, sizeof(SUDO_UART));
    if (UART->SUDO_Handler == NULL) {
        k_free(UART);
        return NULL;
    }
    UART->SUDO_Handler->SUDO_Transmit = Transmit_Func_Ptr;
    UART->SUDO_Handler->SUDO_Receive  = Receive_Func_Ptr;

    UART->RX_Pipe_Storage = k_calloc(UART_RX_BUFF_SIZE, 1);
    if (UART->RX_Pipe_Storage) {
        UART->RX_Pipe_Size = UART_RX_BUFF_SIZE;
        pipe_init(&UART->RX_Pipe, UART->RX_Pipe_Storage, UART->RX_Pipe_Size);
    } else {
        UART->RX_Pipe_Size = 0;
    }

    /* SUDO UARTs don't use DMA buffers - initialize to NULL */
    UART->DMA_RX_Buf[0] = NULL;
    UART->DMA_RX_Buf[1] = NULL;
    UART->DMA_RX_Buf_Size = 0;
    UART->New_Data_Available = false;

    UART->Thread_Stack_Size = sizeof(UART->Thread_Stack);

    UART->Queue_Length = 16;
    UART->Queue_Storage = k_calloc(UART->Queue_Length, sizeof(void*));
    if (UART->Queue_Storage == NULL) {
        k_free(UART->SUDO_Handler);
        k_free(UART);
        LOG_ERR("Init_SUDO_UART: queue storage malloc failed");
        return NULL;
    }

    msgq_init(&UART->TX_Queue, UART->Queue_Storage, sizeof(void*), UART->Queue_Length);
    sem_init(&UART->TX_Done_Sem, 0, 1);
    mutex_init(&UART->state_mutex);

    k_thread_create(&UART->Thread.thread, UART->Thread_Stack, UART->Thread_Stack_Size,
                    UART_Thread_Entry, UART, NULL, NULL,
                    K_PRIO_PREEMPT(CONFIG_UARTTHREADED_THREAD_PRIORITY), 0, K_NO_WAIT);
    UART->Thread.tid = &UART->Thread.thread;

    if (mutex_lock(&g_uart_registry_lock, -1) == 0) {
        if (g_uart_registry_count < UART_REGISTRY_MAX) {
            g_uart_registry[g_uart_registry_count++] = UART;
        } else {
            LOG_ERR("Init_SUDO_UART: Maximum UART instances reached (post-init)");
            mutex_unlock(&g_uart_registry_lock);
            thread_abort(&UART->Thread);
            if (UART->Queue_Storage) k_free(UART->Queue_Storage);
            if (UART->RX_Pipe_Storage) k_free(UART->RX_Pipe_Storage);
            if (UART->SUDO_Handler) k_free(UART->SUDO_Handler);
            k_free(UART);
            return NULL;
        }
        mutex_unlock(&g_uart_registry_lock);
    }

    return UART;
}

void Enable_UART(tUART * UART){
    if (!UART) return;
    if (mutex_lock(&UART->state_mutex, -1) == 0) {
         UART->TX_Buffer = NULL;
         UART->Currently_Transmitting = false;
         UART->UART_Enabled = true;

         /* Re-enable async RX for DMA UARTs */
         if (UART->Use_DMA && UART->UART_Handle && UART->DMA_RX_Buf[0]) {
             /* Reset notification flag */
             UART->New_Data_Available = false;

             /* Re-enable async RX with first buffer */
             int rc = uart_rx_enable(UART->UART_Handle, UART->DMA_RX_Buf[0],
                                     UART->DMA_RX_Buf_Size, SYS_FOREVER_US);
             if (rc != 0) {
                 LOG_ERR("Enable_UART: uart_rx_enable failed: %d", rc);
             }
         }

         mutex_unlock(&UART->state_mutex);
    }
}

void Disable_UART(tUART * UART){
    if (!UART) return;

    /* Disable UART under mutex protection to stop new transmits */
    if (mutex_lock(&UART->state_mutex, -1) == 0) {
        UART->UART_Enabled = false;
        mutex_unlock(&UART->state_mutex);
    }

    /* Wait for any ongoing transmission to complete */
    UART_Flush_TX(UART);

    if (UART->Use_DMA == true && UART->UART_Handle){
        LOG_INF("Disable_UART: waiting for RX to disable");
        uart_rx_disable(UART->UART_Handle);
        LOG_INF("Disable_UART: RX disabled");
    }

    /* Now clean up remaining queue items under mutex protection */
    if (mutex_lock(&UART->state_mutex, -1) == 0) {
        TX_Node * node;
        while(msgq_get(&UART->TX_Queue, &node, 0) == 0){
            if(node){
                k_free(node->Data);
                k_free(node);
            }
        }
        if(UART->TX_Buffer){
            k_free(UART->TX_Buffer->Data);
            k_free(UART->TX_Buffer);
            UART->TX_Buffer = NULL;
        }
        UART->Currently_Transmitting = false;
        mutex_unlock(&UART->state_mutex);
    }
}

void UART_Delete(tUART * UART){
   if(!UART) return;

    Disable_UART(UART);

   if (mutex_lock(&g_uart_registry_lock, -1) == 0) {
       for (size_t i = 0; i < g_uart_registry_count; ++i) {
           if (g_uart_registry[i] == UART) {
               g_uart_registry[i] = g_uart_registry[--g_uart_registry_count];
               break;
           }
       }
       mutex_unlock(&g_uart_registry_lock);
   }

    /* Acquire instance mutex to wait for/serialize with any in-progress operations */
    if (mutex_lock(&UART->state_mutex, -1) == 0) {
        /* Now safe to abort thread and free resources */
        thread_abort(&UART->Thread);

        if(UART->Queue_Storage) k_free(UART->Queue_Storage);
        if(UART->SUDO_Handler) k_free(UART->SUDO_Handler);

        if (UART->RX_Pipe_Storage) k_free(UART->RX_Pipe_Storage);

        /* Free DMA RX buffers if allocated */
        if (UART->DMA_RX_Buf[0]) k_free(UART->DMA_RX_Buf[0]);
        if (UART->DMA_RX_Buf[1]) k_free(UART->DMA_RX_Buf[1]);

        /* release instance mutex and free the struct */
        mutex_unlock(&UART->state_mutex);
    }

    k_free(UART);
}

int8_t UART_Add_Transmit(tUART * UART, uint8_t * Data, uint16_t Data_Size){
    if (!UART || !Data) {
        LOG_WRN("UART_Add_Transmit: Invalid parameters");
        return 0;
    }

    /* If data size is too big, fails; */
    if (Data_Size > MAX_TX_BUFF_SIZE){
        LOG_WRN("UART_Add_Transmit: data too big (%u)", Data_Size);
        return 0;
    }

    TX_Node *node = (TX_Node *)k_calloc(1, sizeof(TX_Node));
    if (node == NULL) {
        LOG_ERR("UART_Add_Transmit: malloc node failed");
        return -1;
    }

    uint8_t *data_To_Add = (uint8_t *)k_calloc(1, Data_Size);
    if (data_To_Add == NULL) {
        k_free(node);
        LOG_ERR("UART_Add_Transmit: malloc data failed");
        return -1;
    }

    memcpy(data_To_Add, Data, Data_Size);
    node->Data = data_To_Add;
    node->Data_Size = Data_Size;

    TX_Node *ptr_val = node;
    /* protect queue put and check against deletion */
    if (mutex_lock(&UART->state_mutex, -1) != 0) {
        k_free(data_To_Add);
        k_free(node);
        return 0;
    }

    /* Check if transmits are enabled under mutex protection */
    if (!UART->UART_Enabled) {
        k_free(data_To_Add);
        k_free(node);
        mutex_unlock(&UART->state_mutex);
        LOG_WRN("UART_Add_Transmit: UART disabled");
        return 0;
    }

    if (msgq_put(&UART->TX_Queue, &ptr_val, 0) != 0) {
        k_free(data_To_Add);
        k_free(node);
        mutex_unlock(&UART->state_mutex);
        return 0; /* queue full */
    }
    mutex_unlock(&UART->state_mutex);
    return Data_Size;
}

void UART_Receive(tUART * UART, uint8_t * Data, uint16_t * Data_Size){
    *Data_Size = 0;
    if(!UART) return;

    if (mutex_lock(&UART->state_mutex, -1) != 0) return;
    if (!UART->UART_Enabled) {
        mutex_unlock(&UART->state_mutex);
        return;
    }

    if (UART->RX_Pipe_Size == 0) {
        mutex_unlock(&UART->state_mutex);
        return;
    }

    size_t bytes_copied = 0;
    int rc = pipe_get(&UART->RX_Pipe, Data, UART->RX_Pipe_Size, &bytes_copied, 0);
    if (rc == 0 || bytes_copied > 0) {
        *Data_Size = (uint16_t)bytes_copied;
    } else {
        *Data_Size = 0;
    }

    mutex_unlock(&UART->state_mutex);
}

int8_t UART_SUDO_Receive(tUART * UART, uint8_t * Data, uint16_t * Data_Size){
    if (!UART || !Data || !Data_Size) {
        if (Data_Size) *Data_Size = 0;
        return 0;
    }
    if(UART->SUDO_Handler == NULL)
        return 0;
    UART->SUDO_Handler->SUDO_Receive(UART, Data, Data_Size);
    return (int8_t)(*Data_Size);
}

void UART_Flush_TX(tUART * uart)
{
    if (!uart) return;

    /* Check enabled status under mutex protection */
    if (mutex_lock(&uart->state_mutex, -1) != 0) return;
    bool enabled = uart->UART_Enabled;
    mutex_unlock(&uart->state_mutex);

    if (!enabled) return;

    while(1){
        if (mutex_lock(&uart->state_mutex, -1) != 0) return;
        bool queue_empty = (msgq_num_used(&uart->TX_Queue) == 0);
        bool not_transmitting = !uart->Currently_Transmitting;
        mutex_unlock(&uart->state_mutex);

        if (queue_empty && not_transmitting)
            break;
        k_msleep(1);
    }
}


// We might need this for synchornizing baudrate for our network protocol
void Modify_UART_Baudrate(tUART * UART, int32_t New_Baudrate){
    if(!UART || !UART->UART_Enabled || !UART->UART_Handle) {
        return;
    }
    UART_Flush_TX(UART);

    const struct device *dev = (const struct device *)UART->UART_Handle;

    uart_rx_disable(dev);

#ifndef __ZEPHYR__
    struct uart_config { int baudrate; };
#endif

    struct uart_config cfg;
    int rc = uart_config_get(dev, &cfg);
    if (rc != 0) {
        LOG_ERR("Modify_UART_Baudrate: uart_config_get failed: %d", rc);
    } else {
        cfg.baudrate = New_Baudrate;
        rc = uart_configure(dev, &cfg);
        if (rc) {
            LOG_ERR("Modify_UART_Baudrate: uart_configure failed: %d", rc);
        } else {
            LOG_INF("Modify_UART_Baudrate: set baud to %d", New_Baudrate);
        }
    }
}