/*
 * UARTThreaded.h
 *
 *  Based on UART.h
 *  Created on: Feb 19, 2025
 *      Author: jason.peng
 * 
 *  Modified on: Aug 17, 2025
 *      Author: buh07
 */

#ifndef UART_THREADED_H_
#define UART_THREADED_H_

#include "../../Inc/main.h"
#include "../../Middlewares/Console/console.h"
#include "tx_api.h"

#ifdef __cplusplus
extern "C" {
#endif

// Default size in bytes for the received buffer.
#define UART_RX_BUFF_SIZE        512
#define MAX_TX_BUFF_SIZE         2048

typedef struct {
    uint8_t * Data;      // data array in ascii
    uint16_t Data_Size;  // size in bytes
} TX_Node;

// Forward declaration so SUDO_UART can use tUART*
typedef struct tUART tUART;

typedef struct {
    void * (*SUDO_Transmit)(tUART * UART, uint8_t * Data, uint16_t Data_Size);
    void * (*SUDO_Receive)(tUART * UART, uint8_t * Data, uint16_t * Data_Size);
} SUDO_UART;

struct tUART {
    UART_HandleTypeDef * UART_Handle;
    bool Use_DMA;
    volatile bool UART_Enabled;
    uint8_t RX_Buffer[UART_RX_BUFF_SIZE];
    volatile uint16_t RX_Buff_Tail_Idx;
    volatile uint16_t RX_Buff_Head_Idx;

    // ThreadX primitives
    TX_QUEUE        TX_Queue;          // queue of TX_Node * (stored as ULONG)
    TX_SEMAPHORE    TX_Done_Sem;       // signaled by Tx complete IRQ
    TX_MUTEX        RX_Mutex;          // protect RX indices
    TX_THREAD       Thread;            // UART worker thread

    // Allocations for RTOS objects
    VOID *          Thread_Stack;      // malloc'd stack
    ULONG           Thread_Stack_Size; 
    VOID *          Queue_Storage;     // malloc'd storage for queue
    ULONG           Queue_Length;      // number of entries in queue

    TX_Node *       TX_Buffer;         // currently transmitting buffer
    volatile bool   Currently_Transmitting;

    SUDO_UART *     SUDO_Handler;      // optional SUDO handler
};


void Init_UART_CallBack_Queue(void);
tUART * Init_DMA_UART(UART_HandleTypeDef * UART_Handle);
tUART * Init_SUDO_UART(void * (*Transmit_Func_Ptr)(tUART*, uint8_t*, uint16_t), void * (*Recieve_Func_Ptr)(tUART*, uint8_t*, uint16_t*));
void Enable_UART(tUART * UART);
void Disable_UART(tUART * UART);
int8_t UART_Add_Transmit(tUART * UART, uint8_t * Data, uint8_t Data_Size);
void UART_Receive(tUART * UART, uint8_t * Data, uint16_t * Data_Size);
int8_t UART_SUDO_Recieve(tUART * UART, uint8_t * Data, uint16_t * Data_Size);
void Modify_UART_Baudrate(tUART * UART, int32_t New_Baudrate);
void UART_Flush_TX(tUART * UART);
void UART_Delete(tUART * UART);

// HAL callbacks
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif // UART_THREADED_H_
