
/*
* UARTThreaded.c
*
*  Based on UART.c
*  Created on: Feb 19, 2025
*      Author: jason.peng
*  
*  Modified on: Aug 18, 2025
*      Author: buh07
*/
#include "UARTAzure.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(uartthreaded, LOG_LEVEL_INF);

#define UART_REGISTRY_MAX 8
static tUART * g_uart_registry[UART_REGISTRY_MAX];
static uint32_t g_uart_registry_count = 0;

/* Forward declaration of the worker entry with original signature */
static void UART_Thread_Entry(ULONG input);

/* File-scope stacks for thread(s) */
K_THREAD_STACK_DEFINE(uart_thread_stack0, 1024);
K_THREAD_STACK_DEFINE(sudo_uart_thread_stack0, 1024);

/* Zephyr UART async callback */
static void uart_cb(const struct device *dev, struct uart_event *evt, void *user_data)
{
    tUART *uart = (tUART *)user_data;
    if (!uart) return;

    switch (evt->type) {
    case UART_TX_DONE:
        k_sem_give(&uart->TX_Done_Sem);
        break;
    case UART_RX_RDY: {
        size_t off = evt->data.rx.offset;
        size_t len = evt->data.rx.len;
        const uint8_t *buf = evt->data.rx.buf + off;
        k_mutex_lock(&uart->RX_Mutex, K_FOREVER);
        for (size_t i = 0; i < len; ++i) {
            uart->RX_Buffer[uart->RX_Buff_Head_Idx++] = buf[i];
            if (uart->RX_Buff_Head_Idx >= UART_RX_BUFF_SIZE) uart->RX_Buff_Head_Idx = 0;
        }
        k_mutex_unlock(&uart->RX_Mutex);
        break;
    }
    case UART_RX_DISABLED:
        /* keep RX enabled continuously */
        uart_rx_enable(dev, uart->RX_Tmp, sizeof(uart->RX_Tmp), K_FOREVER);
        break;
    case UART_RX_BUF_REQUEST:
    case UART_RX_BUF_RELEASED:
    case UART_TX_ABORTED:
    default:
        break;
    }
}

/* Wrapper to keep original thread entry signature */
static void zephyr_uart_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p2); ARG_UNUSED(p3);
    UART_Thread_Entry((unsigned long)p1);
}

void Init_UART_CallBack_Queue(void){
   memset(g_uart_registry, 0, sizeof(g_uart_registry));
   g_uart_registry_count = 0;
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
tUART * Init_DMA_UART(UART_HandleTypeDef * UART_Handle){
   tUART * UART = (tUART*)k_calloc(1, sizeof(tUART));
   if (UART == NULL){
       LOG_ERR("Init_DMA_UART: malloc failed");
       return NULL;
   }

   UART->UART_Handle = UART_Handle; /* const struct device * */
   UART->Use_DMA = true;
   UART->UART_Enabled = true;
   UART->TX_Buffer = NULL;
   UART->Currently_Transmitting = false;
   UART->RX_Buff_Head_Idx = 0;
   UART->RX_Buff_Tail_Idx = 0;
   UART->SUDO_Handler = NULL;

   /* Register for callbacks (kept for parity with original) */
   if (g_uart_registry_count < UART_REGISTRY_MAX){
       g_uart_registry[g_uart_registry_count++] = UART;
   }

   /* Create Zephyr primitives */
   UART->Thread_Stack_Size = 1024;
   UART->Thread_Stack = uart_thread_stack0;

   UART->Queue_Length = 16;
   /* Queue stores pointers; use void* sized messages */
   UART->Queue_Storage = k_calloc(UART->Queue_Length, sizeof(void*));
   if(UART->Queue_Storage == NULL){
       LOG_ERR("Init_DMA_UART: queue storage malloc failed");
       return NULL;
   }

   k_msgq_init(&UART->TX_Queue, UART->Queue_Storage, sizeof(void*), UART->Queue_Length);
   k_sem_init(&UART->TX_Done_Sem, 0, 1);
   k_mutex_init(&UART->RX_Mutex);

   k_thread_create(&UART->Thread, UART->Thread_Stack, UART->Thread_Stack_Size,
                   zephyr_uart_thread_entry, UART, NULL, NULL,
                   K_PRIO_PREEMPT(CONFIG_UARTTHREADED_THREAD_PRIORITY), 0, K_NO_WAIT);

   /* set up RX via async API */
   int rc;
   rc = uart_callback_set(UART->UART_Handle, uart_cb, UART);
   if (rc) {
       LOG_ERR("uart_callback_set failed: %d", rc);
       return UART; /* allow caller to decide; RX will not work */
   }
   rc = uart_rx_enable(UART->UART_Handle, UART->RX_Tmp, sizeof(UART->RX_Tmp), K_FOREVER);
   if (rc) {
       LOG_ERR("uart_rx_enable failed: %d", rc);
       /* Leave initialized but without RX */
   }

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
tUART * Init_SUDO_UART(void * (*Transmit_Func_Ptr)(tUART*, uint8_t*, uint16_t), void * (*Receive_Func_Ptr)(tUART*, uint8_t*, uint16_t*)){
   tUART * UART = (tUART*)k_calloc(1, sizeof(tUART));
   if (UART == NULL){
       LOG_ERR("Init_SUDO_UART: malloc failed");
       return NULL;
   }

   UART->UART_Handle = NULL;
   UART->Use_DMA = false;
   UART->UART_Enabled = true;
   UART->TX_Buffer = NULL;
   UART->Currently_Transmitting = false;
   UART->RX_Buff_Head_Idx = 0;
   UART->RX_Buff_Tail_Idx = 0;

   /* Allocate SUDO handler context */
   UART->SUDO_Handler = (SUDO_UART *)k_calloc(1, sizeof(SUDO_UART));
   if(UART->SUDO_Handler == NULL){
       k_free(UART);
       return NULL;
   }
   UART->SUDO_Handler->SUDO_Transmit = Transmit_Func_Ptr;
   UART->SUDO_Handler->SUDO_Receive  = Receive_Func_Ptr;

   /* Create Zephyr primitives similar to DMA UART */
   UART->Thread_Stack_Size = 1024;
   UART->Thread_Stack = sudo_uart_thread_stack0;

   UART->Queue_Length = 16;
   UART->Queue_Storage = k_calloc(UART->Queue_Length, sizeof(void*));
   if(UART->Queue_Storage == NULL){
       k_free(UART->SUDO_Handler);
       k_free(UART);
       LOG_ERR("Init_SUDO_UART: queue storage malloc failed");
       return NULL;
   }

   k_msgq_init(&UART->TX_Queue, UART->Queue_Storage, sizeof(void*), UART->Queue_Length);
   k_sem_init(&UART->TX_Done_Sem, 0, 1);
   k_mutex_init(&UART->RX_Mutex);

   k_thread_create(&UART->Thread, UART->Thread_Stack, UART->Thread_Stack_Size,
                   zephyr_uart_thread_entry, UART, NULL, NULL,
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
static void UART_Thread_Entry(ULONG input)
{
   tUART * UART = (tUART *)input;

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
   UART->RX_Buff_Head_Idx = 0;
   UART->RX_Buff_Tail_Idx = 0;
   UART->UART_Enabled = true;

   if(UART->UART_Handle){
       int rc = uart_rx_enable(UART->UART_Handle, UART->RX_Tmp, sizeof(UART->RX_Tmp), K_FOREVER);
       if (rc) {
           LOG_ERR("Enable_UART: uart_rx_enable failed: %d", rc);
       }
   }
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
int8_t UART_Add_Transmit(tUART * UART, uint8_t * Data, uint8_t Data_Size){
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

    k_mutex_lock(&UART->RX_Mutex, K_FOREVER);
    while (UART->RX_Buff_Tail_Idx != UART->RX_Buff_Head_Idx){
        Data[*Data_Size] = UART->RX_Buffer[UART->RX_Buff_Tail_Idx++];
        if (UART->RX_Buff_Tail_Idx >= UART_RX_BUFF_SIZE)
            UART->RX_Buff_Tail_Idx = 0;
        (*Data_Size)++;
    }

    k_mutex_unlock(&UART->RX_Mutex);
}

int8_t UART_SUDO_Recieve(tUART * UART, uint8_t * Data, uint16_t * Data_Size){
    if(UART->SUDO_Handler == NULL)
        return 0;
    UART->SUDO_Handler->SUDO_Receive(UART, Data, Data_Size);
    return (int8_t)(*Data_Size);
}

void Modify_UART_Baudrate(tUART * UART, int32_t New_Baudrate){
    if(!UART->UART_Enabled)
        return;

    /* Flush any messages queued to go out */
    UART_Flush_TX(UART);

    /* Stop the receiver DMA -> does DMA stop clear the RX Buffer? */
        uart_rx_disable(UART->UART_Handle);
    UART->RX_Buff_Tail_Idx = 0;

    struct uart_config cfg;
    int rc = uart_config_get(UART->UART_Handle, &cfg);
    if (rc == 0) {
        cfg.baudrate = New_Baudrate;
        rc = uart_configure(UART->UART_Handle, &cfg);
        if (rc) {
            LOG_ERR("Modify_UART_Baudrate: uart_configure failed: %d", rc);
        }
    } else {
        LOG_ERR("Modify_UART_Baudrate: uart_config_get failed: %d", rc);
    }

    rc = uart_rx_enable(UART->UART_Handle, UART->RX_Tmp, sizeof(UART->RX_Tmp), K_FOREVER);
    if (rc) {
        LOG_ERR("Modify_UART_Baudrate: uart_rx_enable failed: %d", rc);
    }
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

/* HAL stubs retained for compatibility, not used */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    ARG_UNUSED(huart);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    ARG_UNUSED(huart);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    ARG_UNUSED(huart);
}