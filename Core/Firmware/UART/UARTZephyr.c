
/*
* UARTZephyr.c
*
*  Based on UART.c
*  Created on: Feb 19, 2025
*      Author: jason.peng
*  
*  Modified on: Aug 18, 2025
*      Author: buh07
*/
#include "UARTZephyr.h"
#include "queue.c"
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <string.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(uartthreaded, LOG_LEVEL_INF);

/* Static-check fallbacks: if kernel structs aren't visible to the analyzer, provide opaque typedefs */
#ifndef __ZEPHYR__
typedef struct k_poll_event k_poll_event_t;
typedef struct k_poll_signal k_poll_signal_t;
#endif

#ifndef CONFIG_UARTTHREADED_THREAD_PRIORITY
#define CONFIG_UARTTHREADED_THREAD_PRIORITY 5
#endif

#define UART_REGISTRY_MAX 8
static tUART * g_uart_registry[UART_REGISTRY_MAX];
static uint32_t g_uart_registry_count = 0;

static void UART_Thread_Entry(void *p1, void *p2, void *p3);

/* Polling primitives */
/* event queue provided by queue.c (peek-capable) */
static void *g_uart_event_queue = NULL; /* opaque queue handle from queue.c */
static struct k_work poll_work;
static struct k_timer poll_timer;
static struct k_poll_signal g_poll_signal;
static struct k_pipe g_uart_pipes[UART_REGISTRY_MAX];
static uint8_t g_uart_pipe_buf[UART_REGISTRY_MAX][UART_RX_BUFF_SIZE];
/* poll thread */
#define UART_POLL_STACK_SIZE 512
K_THREAD_STACK_DEFINE(uart_poll_stack, UART_POLL_STACK_SIZE);
static struct k_thread uart_poll_thread_data;
static bool g_polling_started = false;
static void uart_poll_work_handler(struct k_work *work);
static void poll_timer_handler(struct k_timer *timer){ ARG_UNUSED(timer); k_work_submit(&poll_work); }
static void uart_poll_thread(void *p1, void *p2, void *p3);

static void uart_poll_thread(void *p1, void *p2, void *p3);
static void uart_poll_timer_handler(struct k_timer *timer){ ARG_UNUSED(timer); }

/* (no polling primitives by default) */

/* Poll work: push a token into FIFO so the poll thread wakes and polls UARTs */
static void uart_poll_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    /* push a simple token into FIFO (use pointer to sentinel) */
    static void *token = (void*)1;
    if (g_uart_event_queue) queue_put(g_uart_event_queue, token);
    k_poll_signal_raise(&g_poll_signal, 0);
}

void Init_UART_CallBack_Queue(void){
    memset(g_uart_registry, 0, sizeof(g_uart_registry));
    g_uart_registry_count = 0;
       /* init FIFO, work, timer, pipes */
        /* init peekable queue from queue.c */
        g_uart_event_queue = queue_init();
        k_work_init(&poll_work, uart_poll_work_handler);
        k_timer_init(&poll_timer, poll_timer_handler, NULL);
        k_poll_signal_init(&g_poll_signal);
       for (size_t i = 0; i < UART_REGISTRY_MAX; ++i) {
           k_pipe_init(&g_uart_pipes[i], g_uart_pipe_buf[i], sizeof(g_uart_pipe_buf[i]));
       }

       /* start poll thread and timer once */
       if (!g_polling_started) {
           k_thread_create(&uart_poll_thread_data, uart_poll_stack, UART_POLL_STACK_SIZE,
                           uart_poll_thread, NULL, NULL, NULL,
                           K_PRIO_PREEMPT(CONFIG_UARTTHREADED_THREAD_PRIORITY), K_ESSENTIAL, K_NO_WAIT);
           k_timer_start(&poll_timer, K_MSEC(10), K_MSEC(10));
           g_polling_started = true;
       }
}

    /* poll thread: wait on poll signal and drain the peekable queue; poll all registered UARTs */
static void uart_poll_thread(void *p1, void *p2, void *p3)
    {
        ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
        struct k_poll_event evt;
        while (1) {
            k_poll_event_init(&evt, K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &g_poll_signal);
            k_poll(&evt, 1, K_FOREVER);

            /* drain queue using peek then get */
            while (g_uart_event_queue && queue_peek(g_uart_event_queue) != NULL) {
                void *tok = queue_get(g_uart_event_queue);
                ARG_UNUSED(tok);

                /* poll each registered UART for available bytes */
                for (size_t i = 0; i < g_uart_registry_count; ++i) {
                    tUART *u = g_uart_registry[i];
                    if (!u || !u->UART_Handle || !u->UART_Enabled) continue;

                    uint8_t ch;
                    while (uart_poll_in(u->UART_Handle, &ch) == 0) {
                        /* write received byte directly into the per-UART pipe */
                        size_t bytes_written = 0;
                        int rc = k_pipe_write(&u->RX_Pipe, &ch, 1, &bytes_written, K_NO_WAIT);
                        ARG_UNUSED(rc);
                        ARG_UNUSED(bytes_written);
                    }
            }
        }
    }

/** 
*@brief: malloc a UART, and initialize UART struct members for a UART using DMA. Add it to the callback
* handles queue for when you want to do a callback to match it, then start a task for transmitting the 
* UART (checking bufer and transmitting accordingly). Updates memory trackers for the UART task, and 
* sets up the DMA Access. DMA never needs to RX'd - it will automatically load into UART RX
*
* @params: UART_Handle returned from STM32 HAL.
* 
* @returns: tUART of UART initialized
*/
tUART * Init_DMA_UART(const struct device *uart_dev)
{
    if (!uart_dev) {
        LOG_ERR("Init_DMA_UART: uart_dev is NULL");
        return NULL;
    }
    if (!device_is_ready(uart_dev)) {
        LOG_ERR("Init_DMA_UART: device not ready");
        return NULL;
    }

    tUART * UART = (tUART*)k_calloc(1, sizeof(tUART));
    if (UART == NULL){
        LOG_ERR("Init_DMA_UART: malloc failed");
        return NULL;
    }

    UART->UART_Handle = uart_dev;
    UART->Use_DMA = true;
    UART->UART_Enabled = true;
    UART->TX_Buffer = NULL;
    UART->Currently_Transmitting = false;
    /* legacy ring indices removed; RX now uses k_pipe */
    UART->SUDO_Handler = NULL;

    /* allocate per-instance pipe storage and init pipe */
    UART->RX_Pipe_Storage = k_calloc(UART_RX_BUFF_SIZE, 1);
    if (UART->RX_Pipe_Storage) {
        UART->RX_Pipe_Size = UART_RX_BUFF_SIZE;
        k_pipe_init(&UART->RX_Pipe, UART->RX_Pipe_Storage, UART->RX_Pipe_Size);
    } else {
        UART->RX_Pipe_Size = 0;
    }

    /* Register for callbacks*/
    if (g_uart_registry_count < UART_REGISTRY_MAX){
        g_uart_registry[g_uart_registry_count++] = UART;
    }

    /* Create Zephyr primitives */
    UART->Thread_Stack_Size = K_THREAD_STACK_SIZEOF(UART->Thread_Stack);

    UART->Queue_Length = 16;
    UART->Queue_Storage = k_calloc(UART->Queue_Length, sizeof(void*));
    if(UART->Queue_Storage == NULL){
        LOG_ERR("Init_DMA_UART: queue storage malloc failed");
        k_free(UART);
        return NULL;
    }

    k_msgq_init(&UART->TX_Queue, UART->Queue_Storage, sizeof(void*), UART->Queue_Length);
    k_sem_init(&UART->TX_Done_Sem, 0, 1);
    /* RX spinlock is statically zeroed; no init required */

    /* start the UART worker thread */
    k_thread_create(&UART->Thread, UART->Thread_Stack, UART->Thread_Stack_Size,
                    UART_Thread_Entry, UART, NULL, NULL,
                    K_PRIO_PREEMPT(CONFIG_UARTTHREADED_THREAD_PRIORITY), 0, K_NO_WAIT);

    /* Polling mode: no async callback registration. Poll thread will use uart_poll_in. */

    return UART;
}

/**
*  @brief: Initialize a SUDO UART. mallocs the tUART for the SUDO UART. SUDO UART has no Handle but
* updates the other peripherals including the SUDO Transmit and SUDO Recieve functions for the SUDO UART.
* Starts a task for checking the UART TX transmissions, adds correct memory usage to that task.
*
* @params: Transmit function for SUDO UART, Recieve function for SUDO UART
* 
* @returns: tUART struct initialized and malloc-ed
* */
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
    /* legacy ring indices removed; RX now uses k_pipe */

    /* Allocate SUDO handler context */
    UART->SUDO_Handler = (SUDO_UART *)k_calloc(1, sizeof(SUDO_UART));
    if (UART->SUDO_Handler == NULL) {
        k_free(UART);
        return NULL;
    }
    UART->SUDO_Handler->SUDO_Transmit = Transmit_Func_Ptr;
    UART->SUDO_Handler->SUDO_Receive  = Receive_Func_Ptr;

    /* allocate per-instance pipe storage and init pipe */
    UART->RX_Pipe_Storage = k_calloc(UART_RX_BUFF_SIZE, 1);
    if (UART->RX_Pipe_Storage) {
        UART->RX_Pipe_Size = UART_RX_BUFF_SIZE;
        k_pipe_init(&UART->RX_Pipe, UART->RX_Pipe_Storage, UART->RX_Pipe_Size);
    } else {
        UART->RX_Pipe_Size = 0;
    }

    /* Register */
    if (g_uart_registry_count < UART_REGISTRY_MAX) {
        g_uart_registry[g_uart_registry_count++] = UART;
    }

    /* Create Zephyr primitives similar to DMA UART */
    UART->Thread_Stack_Size = K_THREAD_STACK_SIZEOF(UART->Thread_Stack);

    UART->Queue_Length = 16;
    UART->Queue_Storage = k_calloc(UART->Queue_Length, sizeof(void*));
    if (UART->Queue_Storage == NULL) {
        k_free(UART->SUDO_Handler);
        k_free(UART);
        LOG_ERR("Init_SUDO_UART: queue storage malloc failed");
        return NULL;
    }

    k_msgq_init(&UART->TX_Queue, UART->Queue_Storage, sizeof(void*), UART->Queue_Length);
    k_sem_init(&UART->TX_Done_Sem, 0, 1);

    k_thread_create(&UART->Thread, UART->Thread_Stack, UART->Thread_Stack_Size,
                    UART_Thread_Entry, UART, NULL, NULL,
                    K_PRIO_PREEMPT(CONFIG_UARTTHREADED_THREAD_PRIORITY), 0, K_NO_WAIT);

    return UART;
}

/**
* @Notes-1: Handle the TX in the queue ONLY.
* don't need to handle Rx because Rx is polled by tasks running UART (example - polled by console
* if console is in use)
* If device is active, then UART is being polled for Rx's and continuously in use.
* No long idle periods for UART - program the device and the UART to recieve intermittently, so don't need
* to use RX interrupt.
* 
* @Notes-2: Will need to set up interrupt service for LoRA and power on from sleep;
* need to turn on device when pinged.
* 
* *********************************************************************************************
* *********************************************************************************************
* 
* @brief: Handles the TX in the Queue. Checks if ready to transmit, clears out the previous buffer, 
* dequeues from the transmit buffer and transmits it. USES DMA
* 
* @params: UART struct of UART to be handled.
* 
* @return: None 
*/
static void UART_Thread_Entry(void *p1, void *p2, void *p3)
{
   ARG_UNUSED(p2);
   ARG_UNUSED(p3);
   tUART * UART = (tUART *)p1;

   while(1)
   {
       /* Wait for next TX buffer pointer from queue */
       TX_Node *node = NULL;
       k_msgq_get(&UART->TX_Queue, &node, K_FOREVER);
       if(node == NULL)
           continue;

       UART->TX_Buffer = node;

       if(!UART->UART_Enabled)
       {
           /* drop and free */
           k_free(UART->TX_Buffer->Data);
           k_free(UART->TX_Buffer);
           UART->TX_Buffer = NULL;
           continue;
       }

       UART->Currently_Transmitting = true;
       if(UART->Use_DMA && UART->UART_Handle != NULL)
       {
           int rc = uart_tx(UART->UART_Handle, UART->TX_Buffer->Data, UART->TX_Buffer->Data_Size, SYS_FOREVER_MS);
           if (rc == 0) {
               /* Wait for callback to signal completion */
               k_sem_take(&UART->TX_Done_Sem, K_FOREVER);
           }
       }
       else if (UART->SUDO_Handler != NULL)
       {
           UART->SUDO_Handler->SUDO_Transmit(UART, UART->TX_Buffer->Data, UART->TX_Buffer->Data_Size);
       }

       UART->Currently_Transmitting = false;
       /* Free buffer after transmit */
       k_free(UART->TX_Buffer->Data);
       k_free(UART->TX_Buffer);
       UART->TX_Buffer = NULL;
   }
}

/**
* @brief: Enables UART from disable mode. Reinitializes the queue, reinitializes TX Buffer 
* enables Recieve DMA.

* @params: UART struct of UART being enabled
* 
* @return: None
*/
void Enable_UART(tUART * UART){
   if(UART->UART_Handle)
       ; /* No MSP init in Zephyr */
   UART->TX_Buffer = NULL;
   UART->Currently_Transmitting = false;
   UART->UART_Enabled = true;

    /* RX is handled by poll thread; no uart_rx_enable in polling mode */
}

/**
* @brief: Disables the UART. Flushes the tX Queue, then as soon as the Queue is done recieving, 
* deactivates the DMA and De-inits the UART. Then free the TX Queue and TX buffer so when re-referenced.
* doesn't cause a memory leak.
* 
* @params: UART 
* 
* @return: None
*/
void Disable_UART(tUART * UART){
   UART_Flush_TX(UART);
   /* if using DMA */
   if (UART->Use_DMA == true && UART->UART_Handle){
       /* allow all receiving transmissions to go thru */
       LOG_INF("Disable_UART: waiting for RX to disable");
       uart_rx_disable(UART->UART_Handle);
       LOG_INF("Disable_UART: RX disabled");
   }

   /* Drain queue: try receive and free outstanding nodes */
   TX_Node * node;
   while(k_msgq_get(&UART->TX_Queue, &node, K_NO_WAIT) == 0){
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

   /*Update currently transmitting and UART Enabled*/
   UART->Currently_Transmitting = false;
   UART->UART_Enabled = false;
}

/**
* @brief: Delete UART and clean up all resources including ThreadX primitives, malloced stacks, queue storage
* 
* @params: UART struct to delete
*
* @return: None
*/
void UART_Delete(tUART * UART){
   if(!UART) return;

   Disable_UART(UART);

   /* Delete Zephyr primitives */
   k_thread_abort(&UART->Thread);
   /* k_mutex doesn't need deletion */
   /* k_sem/msgq don't need deletion; free storage if allocated */

   /* Free malloced memory */
   if(UART->Queue_Storage) k_free(UART->Queue_Storage);
   if(UART->SUDO_Handler) k_free(UART->SUDO_Handler);

   k_free(UART);
}

/**
 * @brief: Add Data to the UART Transmit Queue. Checks if UART is Enabled and if Data does not exceed
 * buffer size. If passes checks, malloc-s a new TX node, adds Data to that TX_Node->Data ptr, then 
 * enqueues that new node.
 * 
 * @params: UART to transmit from, pointer to beginning of Data segment, Data_Size
 * 
 * @return: Data_Size if success, 0 if transmit was unsuccessful, -1 for malloc error.
 */
int8_t UART_Add_Transmit(tUART * UART, uint8_t * Data, uint16_t Data_Size){
    /* check if transmits are enabled */
    if (!UART->UART_Enabled){
        LOG_WRN("UART_Add_Transmit: UART disabled");
        return 0;
    }
    /* if data size is too big, return fail; */
    if (Data_Size > MAX_TX_BUFF_SIZE){
        LOG_WRN("UART_Add_Transmit: data too big (%u)", Data_Size);
        return 0;
    }
    /* create a new Tx node */
    TX_Node * to_Node = (TX_Node*)k_malloc(sizeof(TX_Node));
        /* if malloc is successful: */
    if (to_Node != NULL){
        /* malloc for the data */
        uint8_t * data_To_Add =  (uint8_t*)k_malloc(sizeof(uint8_t)*Data_Size);
        /* if malloc for the data is successful: */
        if (data_To_Add != NULL){
        /* copy the data over to the node and enqueue the data: */
            memcpy(data_To_Add, Data, Data_Size);
            to_Node->Data = data_To_Add;
            to_Node->Data_Size = Data_Size;

            TX_Node *ptr_val = to_Node;
            if(k_msgq_put(&UART->TX_Queue, &ptr_val, K_NO_WAIT) != 0){
                k_free(data_To_Add);
                k_free(to_Node);
                return 0;
            }
            return Data_Size;
        }
    }
    /* or else, free the node and pointer (if malloc for data wasn't successful) */
    if(to_Node) k_free(to_Node);
    LOG_ERR("UART_Add_Transmit: malloc error");
    /* return fail */
    return -1;
}

/**
 * @brief: Recieves UART Data to the uint8_t data pointer from the Rx Buffer. If UART is not enabled, does not
 * do any recieving and returns false. If UART is busy, requeues the data entry and 
 * tries to recieve it again to the data buffer. Returns false anyways, but requeues 
 * so that the data is not lost. // needs updating - removed some functionality to make simpler
 * 
 * Because of this, ALL UART Buffer DATA should be a STATIC or MALLOC STORAGE, not
 * temporary storage.
 * 
 * @params: tUART * UART Handle
 * @params: uint8t * Data buffer to store received data
 * @params: uint8t * Ptr to buffer to hold the size of data received.
 * 
 * @return: size of data received. This should be matched to the size of the data
 * that should have been sent for sensitive applications such as GPS data, etc.
 */
void UART_Receive(tUART * UART, uint8_t * Data, uint16_t * Data_Size){
    *Data_Size = 0;
    if (!UART || !UART->UART_Enabled)
        return;
    unsigned int key = k_spin_lock(&UART->RX_Spinlock);

    if (UART->RX_Pipe_Size == 0) {
        k_spin_unlock(&UART->RX_Spinlock, key);
        return; /* pipe not initialized */
    }

    size_t bytes_copied = 0;
    /* read up to buffer size */
    int rc = k_pipe_read(&UART->RX_Pipe, Data, UART_RX_BUFF_SIZE, &bytes_copied, 1, K_NO_WAIT);
    if (rc == 0 || bytes_copied > 0) {
        *Data_Size = (uint16_t)bytes_copied;
    } else {
        *Data_Size = 0;
    }

    k_spin_unlock(&UART->RX_Spinlock, key);
}

int8_t UART_SUDO_Receive(tUART * UART, uint8_t * Data, uint16_t * Data_Size){
    if(UART->SUDO_Handler == NULL)
        return 0;
    UART->SUDO_Handler->SUDO_Receive(UART, Data, Data_Size);
    return (int8_t)(*Data_Size);
}

void Modify_UART_Baudrate(tUART * UART, int32_t New_Baudrate){
    if(!UART || !UART->UART_Enabled || !UART->UART_Handle)
        return;

    /* Flush pending TX */
    UART_Flush_TX(UART);

    const struct device *dev = (const struct device *)UART->UART_Handle;

    /* disable RX while we reconfigure */
    uart_rx_disable(dev);

#ifndef __ZEPHYR__
    /* static-checker fallback: opaque uart_config declaration */
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

    /* RX re-enabled implicitly by poll thread; no uart_rx_enable needed */
}


void UART_Flush_TX(tUART * uart)
{
    if(!uart->UART_Enabled)
        return;

    while(1){
        /* Wait until queue becomes empty and nothing is transmitting */
        if (k_msgq_num_used_get(&uart->TX_Queue) == 0 && !uart->Currently_Transmitting)
            break;
        k_msleep(1);
    }
}