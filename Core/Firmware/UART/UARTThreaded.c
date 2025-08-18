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
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include "UARTThreaded.h"
#include "main.h"

#define UART_REGISTRY_MAX 8
static tUART * g_uart_registry[UART_REGISTRY_MAX];
static uint32_t g_uart_registry_count = 0;

static void UART_Thread_Entry(ULONG input);

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
   tUART * UART = (tUART*)calloc(1, sizeof(tUART));
   if (UART == NULL){
       printf("func INIT UART: malloc failed\r\n");
       return NULL;
   }

   UART->UART_Handle = UART_Handle;
   UART->Use_DMA = true;
   UART->UART_Enabled = true;
   UART->TX_Buffer = NULL;
   UART->Currently_Transmitting = false;
   UART->RX_Buff_Head_Idx = 0;
   UART->RX_Buff_Tail_Idx = 0;
   UART->SUDO_Handler = NULL;

   // Register for callbacks
   if (g_uart_registry_count < UART_REGISTRY_MAX){
       g_uart_registry[g_uart_registry_count++] = UART;
   }

   // Create ThreadX primitives
   UART->Thread_Stack_Size = 1024;
   UART->Thread_Stack = malloc(UART->Thread_Stack_Size);
   if(UART->Thread_Stack == NULL){
       printf("func INIT UART: thread stack malloc failed\r\n");
       free(UART);
       return NULL;
   }

   UART->Queue_Length = 16;
   // Queue stores pointers; use ULONG-sized messages
   uint32_t queue_storage_ulongs = UART->Queue_Length;
   UART->Queue_Storage = malloc(queue_storage_ulongs * sizeof(ULONG));
   if(UART->Queue_Storage == NULL){
       printf("func INIT UART: queue storage malloc failed\r\n");
       free(UART->Thread_Stack);
       free(UART);
       return NULL;
   }

   if(tx_queue_create(&UART->TX_Queue, "UART_TX_Q", TX_1_ULONG, UART->Queue_Storage, queue_storage_ulongs * sizeof(ULONG)) != TX_SUCCESS){
       printf("func INIT UART: tx_queue_create failed\r\n");
       free(UART->Queue_Storage);
       free(UART->Thread_Stack);
       free(UART);
       return NULL;
   }

   if(tx_semaphore_create(&UART->TX_Done_Sem, "UART_TX_DONE", 0) != TX_SUCCESS){
       printf("func INIT UART: tx_semaphore_create failed\r\n");
       tx_queue_delete(&UART->TX_Queue);
       free(UART->Queue_Storage);
       free(UART->Thread_Stack);
       free(UART);
       return NULL;
   }

   if(tx_mutex_create(&UART->RX_Mutex, "UART_RX_MUTEX", TX_NO_INHERIT) != TX_SUCCESS){
       printf("func INIT UART: tx_mutex_create failed\r\n");
       tx_semaphore_delete(&UART->TX_Done_Sem);
       tx_queue_delete(&UART->TX_Queue);
       free(UART->Queue_Storage);
       free(UART->Thread_Stack);
       free(UART);
       return NULL;
   }

   if(tx_thread_create(&UART->Thread, "UART RX/TX", UART_Thread_Entry, (ULONG)UART,
                       UART->Thread_Stack, UART->Thread_Stack_Size,
                       TX_MAX_PRIORITIES - 2, TX_MAX_PRIORITIES - 2,
                       1, TX_AUTO_START) != TX_SUCCESS){
       printf("func INIT UART: tx_thread_create failed\r\n");
       tx_mutex_delete(&UART->RX_Mutex);
       tx_semaphore_delete(&UART->TX_Done_Sem);
       tx_queue_delete(&UART->TX_Queue);
       free(UART->Queue_Storage);
       free(UART->Thread_Stack);
       free(UART);
       return NULL;
   }

   //set up the DMA access
   HAL_UART_Receive_DMA(UART_Handle, UART->RX_Buffer, UART_RX_BUFF_SIZE);

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
   tUART * UART = (tUART*)calloc(1, sizeof(tUART));
   if (UART == NULL){
       printf("func INIT UART: malloc failed\r\n");
       return NULL;
   }

   UART->UART_Handle = NULL;
   UART->Use_DMA = false;
   UART->UART_Enabled = true;
   UART->TX_Buffer = NULL;
   UART->Currently_Transmitting = false;
   UART->RX_Buff_Head_Idx = 0;
   UART->RX_Buff_Tail_Idx = 0;

   // Allocate SUDO handler context
   UART->SUDO_Handler = (SUDO_UART *)calloc(1, sizeof(SUDO_UART));
   if(UART->SUDO_Handler == NULL){
       free(UART);
       return NULL;
   }
   UART->SUDO_Handler->SUDO_Transmit = Transmit_Func_Ptr;
   UART->SUDO_Handler->SUDO_Receive  = Receive_Func_Ptr;

   // Create ThreadX primitives similar to DMA UART
   UART->Thread_Stack_Size = 1024;
   UART->Thread_Stack = malloc(UART->Thread_Stack_Size);
   if(UART->Thread_Stack == NULL){
       free(UART->SUDO_Handler);
       free(UART);
       return NULL;
   }

   UART->Queue_Length = 16;
   uint32_t queue_storage_ulongs = UART->Queue_Length;
   UART->Queue_Storage = malloc(queue_storage_ulongs * sizeof(ULONG));
   if(UART->Queue_Storage == NULL){
       free(UART->Thread_Stack);
       free(UART->SUDO_Handler);
       free(UART);
       return NULL;
   }

   if(tx_queue_create(&UART->TX_Queue, "SUDO_UART_TX_Q", TX_1_ULONG, UART->Queue_Storage, queue_storage_ulongs * sizeof(ULONG)) != TX_SUCCESS){
       free(UART->Queue_Storage);
       free(UART->Thread_Stack);
       free(UART->SUDO_Handler);
       free(UART);
       return NULL;
   }

   if(tx_semaphore_create(&UART->TX_Done_Sem, "SUDO_UART_TX_DONE", 0) != TX_SUCCESS){
       tx_queue_delete(&UART->TX_Queue);
       free(UART->Queue_Storage);
       free(UART->Thread_Stack);
       free(UART->SUDO_Handler);
       free(UART);
       return NULL;
   }

   if(tx_mutex_create(&UART->RX_Mutex, "SUDO_UART_RX_MUTEX", TX_NO_INHERIT) != TX_SUCCESS){
       tx_semaphore_delete(&UART->TX_Done_Sem);
       tx_queue_delete(&UART->TX_Queue);
       free(UART->Queue_Storage);
       free(UART->Thread_Stack);
       free(UART->SUDO_Handler);
       free(UART);
       return NULL;
   }

   if(tx_thread_create(&UART->Thread, "SUDO UART RX/TX", UART_Thread_Entry, (ULONG)UART,
                       UART->Thread_Stack, UART->Thread_Stack_Size,
                       TX_MAX_PRIORITIES - 2, TX_MAX_PRIORITIES - 2,
                       1, TX_AUTO_START) != TX_SUCCESS){
       tx_mutex_delete(&UART->RX_Mutex);
       tx_semaphore_delete(&UART->TX_Done_Sem);
       tx_queue_delete(&UART->TX_Queue);
       free(UART->Queue_Storage);
       free(UART->Thread_Stack);
       free(UART->SUDO_Handler);
       free(UART);
       return NULL;
   }

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
       // Wait for next TX buffer pointer from queue
       ULONG msg_ptr_val;
       if(tx_queue_receive(&UART->TX_Queue, &msg_ptr_val, TX_WAIT_FOREVER) != TX_SUCCESS)
           continue;

       UART->TX_Buffer = (TX_Node *) (uintptr_t) msg_ptr_val;
       if(UART->TX_Buffer == NULL)
           continue;

       if(!UART->UART_Enabled)
       {
           // drop and free
           free(UART->TX_Buffer->Data);
           free(UART->TX_Buffer);
           UART->TX_Buffer = NULL;
           continue;
       }

       UART->Currently_Transmitting = true;
       if(UART->Use_DMA && UART->UART_Handle != NULL)
       {
           HAL_UART_Transmit_DMA(UART->UART_Handle, UART->TX_Buffer->Data, UART->TX_Buffer->Data_Size);
           // Wait for ISR to signal completion
           tx_semaphore_get(&UART->TX_Done_Sem, TX_WAIT_FOREVER);
       }
       else if (UART->SUDO_Handler != NULL)
       {
           UART->SUDO_Handler->SUDO_Transmit(UART, UART->TX_Buffer->Data, UART->TX_Buffer->Data_Size);
       }

       UART->Currently_Transmitting = false;
       // Free buffer after transmit
       free(UART->TX_Buffer->Data);
       free(UART->TX_Buffer);
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
       HAL_UART_MspInit(UART->UART_Handle);
   UART->TX_Buffer = NULL;
   UART->Currently_Transmitting = false;
   UART->RX_Buff_Head_Idx = 0;
   UART->RX_Buff_Tail_Idx = 0;
   UART->UART_Enabled = true;

   if(UART->UART_Handle)
       HAL_UART_Receive_DMA(UART->UART_Handle, UART->RX_Buffer, UART_RX_BUFF_SIZE);
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
   // if using DMA
   if (UART->Use_DMA == true && UART->UART_Handle){
       if (UART->UART_Handle->RxState == HAL_UART_STATE_BUSY_RX){
           // allow all receiving transmissions to go thru
           printf("func Disable UART: Rx is busy, waiting for RX transmission to finish.\r\n");
           while (UART->UART_Handle->RxState == HAL_UART_STATE_BUSY_RX){
               HAL_Delay(1);
           }
           // then deactivate the UART DMA immediately
           HAL_UART_DMAStop(UART->UART_Handle);  // Stop the DMA reception
           printf("func Disable UART: Rx finished. Disabling UART\r\n");
       }
       // Deinit the UART
       HAL_UART_MspDeInit(UART->UART_Handle);
   }

   // Drain queue: try receive and free outstanding nodes
   ULONG msg;
   while(tx_queue_receive(&UART->TX_Queue, &msg, TX_NO_WAIT) == TX_SUCCESS){
       TX_Node * node = (TX_Node *)(uintptr_t)msg;
       if(node){
           free(node->Data);
           free(node);
       }
   }
   if(UART->TX_Buffer){
       free(UART->TX_Buffer->Data);
       free(UART->TX_Buffer);
       UART->TX_Buffer = NULL;
   }

   //Update currently transmitting and UART Enabled
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

   // Delete ThreadX primitives
   tx_thread_delete(&UART->Thread);
   tx_mutex_delete(&UART->RX_Mutex);
   tx_semaphore_delete(&UART->TX_Done_Sem);
   tx_queue_delete(&UART->TX_Queue);

   // Free malloced memory
   if(UART->Thread_Stack) free(UART->Thread_Stack);
   if(UART->Queue_Storage) free(UART->Queue_Storage);
   if(UART->SUDO_Handler) free(UART->SUDO_Handler);

   free(UART);
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
    // check if transmits are enabled
    if (!UART->UART_Enabled){
        printf("Tried to transmit and failed. UART Disabled.\r\n");
        return 0;
    }
    // if data size is too big, return fail;
    if (Data_Size > MAX_TX_BUFF_SIZE){
        printf("Tried to transmit and failed. Transmit Data is too big.\r\n");
        return 0;
    }
    // create a new Tx node
    TX_Node * to_Node = (TX_Node*)malloc(sizeof(TX_Node));
        // if malloc is successful:
    if (to_Node != NULL){
        // malloc for the data
        uint8_t * data_To_Add =  (uint8_t*)malloc(sizeof(uint8_t)*Data_Size);
        // if malloc for the data is successful:
        if (data_To_Add != NULL){
        // copy the data over to the node and enqueue the data:
            memcpy(data_To_Add, Data, Data_Size);
            to_Node->Data = data_To_Add;
            to_Node->Data_Size = Data_Size;

            ULONG ptr_val = (ULONG)(uintptr_t)to_Node;
            if(tx_queue_send(&UART->TX_Queue, &ptr_val, TX_NO_WAIT) != TX_SUCCESS){
                free(data_To_Add);
                free(to_Node);
                return 0;
            }
            return Data_Size;
        }
    }
    // or else, free the node and pointer (if malloc for data wasn't successful)
    if(to_Node) free(to_Node);
    printf("func UART_Add_Trasmit: malloc error.\r\n");
    // return fail
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

    tx_mutex_get(&UART->RX_Mutex, TX_WAIT_FOREVER);
    uint16_t head = UART->RX_Buff_Head_Idx;
    // If using DMA with circular buffer, compute head from CNDTR
    if (UART->Use_DMA && UART->UART_Handle && UART->UART_Handle->hdmarx){
        uint16_t cndtr = UART->UART_Handle->hdmarx->Instance->CNDTR;
        head = (uint16_t)(UART_RX_BUFF_SIZE - cndtr) % UART_RX_BUFF_SIZE;
        UART->RX_Buff_Head_Idx = head;
    }

    while (UART->RX_Buff_Tail_Idx != head){
        Data[*Data_Size] = UART->RX_Buffer[UART->RX_Buff_Tail_Idx++];
        if (UART->RX_Buff_Tail_Idx >= UART_RX_BUFF_SIZE)
            UART->RX_Buff_Tail_Idx = 0;
        (*Data_Size)++;
    }

    tx_mutex_put(&UART->RX_Mutex);
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

    // Flush any messages queued to go out
    UART_Flush_TX(UART);

	// Stop the receiver DMA -> does DMA stop clear the RX Buffer?
	    HAL_UART_DMAStop(UART->UART_Handle);
    UART->RX_Buff_Tail_Idx = 0;

    UART->UART_Handle->Init.BaudRate = New_Baudrate;
    HAL_UART_Init(UART->UART_Handle);

    HAL_UART_Receive_DMA(UART->UART_Handle, UART->RX_Buffer, UART_RX_BUFF_SIZE);
}

void UART_Flush_TX(tUART * uart)
{
    if(!uart->UART_Enabled)
        return;

    while(1){
        ULONG msg;
        UINT had_msg = tx_queue_receive(&uart->TX_Queue, &msg, TX_NO_WAIT);
        if(had_msg == TX_SUCCESS){
            tx_queue_front_send(&uart->TX_Queue, &msg, TX_NO_WAIT);
        }

        if(had_msg != TX_SUCCESS && !uart->Currently_Transmitting)
            break;

        tx_thread_sleep(1);
    }
}

void UART_Delete(tUART * UART){
    if(!UART) return;

    Disable_UART(UART);

    // Best effort to stop thread
    tx_thread_delete(&UART->Thread);
    tx_mutex_delete(&UART->RX_Mutex);
    tx_semaphore_delete(&UART->TX_Done_Sem);
    tx_queue_delete(&UART->TX_Queue);

    if(UART->Thread_Stack) free(UART->Thread_Stack);
    if(UART->Queue_Storage) free(UART->Queue_Storage);
    if(UART->SUDO_Handler) free(UART->SUDO_Handler);

    free(UART);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    // Find who the callback is for
    for(uint32_t c = 0; c < g_uart_registry_count; c++){
        tUART * uart = g_uart_registry[c];
        if(uart && uart->UART_Handle == huart){
            tx_semaphore_put(&uart->TX_Done_Sem);
            return;
        }
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    UNUSED(huart);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    // Find who the callback is for
    for(uint32_t c = 0; c < g_uart_registry_count; c++){
        tUART * uart = g_uart_registry[c];
        if(uart && uart->UART_Handle == huart){
            uart->RX_Buff_Tail_Idx = 0;
            uart->Currently_Transmitting = false;
            HAL_DMA_Abort_IT(uart->UART_Handle->hdmarx);
            HAL_UART_DMAStop(uart->UART_Handle);
            HAL_UART_Receive_DMA(uart->UART_Handle, uart->RX_Buffer, UART_RX_BUFF_SIZE);
        }
    }
}
