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
 
 #include <zephyr/kernel.h>
 #include <zephyr/device.h>
 #include <zephyr/drivers/uart.h>
 #include <zephyr/sys/util.h>
 #include <zephyr/sys/printk.h>
 #include <zephyr/sys/__assert.h>
 #include <string.h>
 #include <stdbool.h>
 #include <stdint.h>
 
 /* Provide ThreadX-like integral type used in original code */
 typedef unsigned long ULONG;
 
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
 
 /*
  * In Zephyr, we map the STM32 HAL type name to the Zephyr device pointer
  * so existing function signatures remain unchanged for callers of this header.
  */
 typedef const struct device UART_HandleTypeDef;
 
 struct tUART {
     UART_HandleTypeDef * UART_Handle;   /* actually const struct device * in Zephyr */
     bool Use_DMA;                       /* kept for compatibility; Zephyr abstracts DMA */
     volatile bool UART_Enabled;
     uint8_t RX_Buffer[UART_RX_BUFF_SIZE];
     volatile uint16_t RX_Buff_Tail_Idx;
     volatile uint16_t RX_Buff_Head_Idx;
 
     /* Zephyr kernel primitives (replace ThreadX) */
     struct k_msgq   TX_Queue;           // queue of TX_Node * (stored as void*)
     struct k_sem    TX_Done_Sem;        // signaled by Tx complete event
     struct k_mutex  RX_Mutex;           // protect RX indices
     struct k_thread Thread;             // UART worker thread
 
     /* Allocations for RTOS objects */
     k_thread_stack_t * Thread_Stack;    // allocated stack
     size_t           Thread_Stack_Size; 
     void *           Queue_Storage;     // storage for msgq
     size_t           Queue_Length;      // number of entries in queue
 
     TX_Node *       TX_Buffer;          // currently transmitting buffer
     volatile bool   Currently_Transmitting;
 
     SUDO_UART *     SUDO_Handler;       // optional SUDO handler
 
     /* Async RX helper buffer */
     uint8_t         RX_Tmp[64];
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
 
 /* HAL callbacks are not used in Zephyr implementation; keep prototypes for compatibility but no-op. */
 static inline void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) { ARG_UNUSED(huart); }
 static inline void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) { ARG_UNUSED(huart); }
 static inline void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) { ARG_UNUSED(huart); }
 
 #ifdef __cplusplus
 }
 #endif
 
 #endif // UART_THREADED_H_ 