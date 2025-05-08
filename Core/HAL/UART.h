/*
 * UART.h
 *
 *  Created on: Sep 19, 2024
 *      Author: jason.peng
 */

#ifndef UART_UART_H_
#define UART_UART_H_

#include "../Inc/main.h"
#include "../Middlewares/Queue/queue.h"
#include "../Middlewares/Console/console.h"




#ifdef __cplusplus
extern "C" {
#endif

// Default size in bytes for the received buffer.
#define UART_RX_BUFF_SIZE		512 //why 512? 
#define MAX_TX_BUFF_SIZE        2048

typedef struct {
    uint8_t * Data; // data array in ascii
    uint8_t * Data_Size;
} TX_Node;

typedef struct {
    UART_HandleTypeDef * UART_Handle;
    bool Use_DMA;
    bool UART_Enabled;
    uint8_t * RX_Buffer[UART_RX_BUFF_SIZE];
    uint8_t RX_Buff_Tail_Idx;
    uint8_t RX_Buff_Head_Idx;
    Queue * TX_Queue;
    TX_Node * TX_Buffer;
    volatile bool Currently_Transmitting;
    uint8_t Task_ID;
    SUDO_UART * SUDO_Handler;
    tUART_Repeat_Receive * Repeat_Handle;
} tUART;

typedef struct {
    tUART * UART;
    uint8_t Task_ID;
    Queue * Repeat_Queue;
} tUART_Repeat_Receive;

typedef struct {
    uint8_t * Data_Buff;
    uint8_t * Data_Size_Buff;
    bool * success_Buff;
    bool done;
} tUART_Repeat_Node;

typedef struct {
    void * (*SUDO_Transmit)(tUART * UART, uint8_t * Data, uint16_t Data_Size);
    void * (*SUDO_Receive)(tUART * UART, uint8_t * Data, uint16_t * Data_Size);
} SUDO_UART;


void Init_UART_CallBack_Queue(void);
tUART * Init_DMA_UART(UART_HandleTypeDef * UART_Handle);
tUART * Init_SUDO_UART(void * (*Transmit_Func_Ptr)(uint8_t*, uint8_t), void * (*Recieve_Func_Ptr)(uint8_t*, uint8_t));
void Enable_UART(tUART * UART);
void Disable_UART(tUART * UART);
int8_t UART_Add_Transmit(tUART * UART, uint8_t * Data, uint8_t Data_Size);
int8_t UART_Receive(tUART * UART, uint8_t * Data, uint8_t * Data_Size);
int8_t UART_SUDO_Recieve(tUART * UART, uint8_t * Data, uint8_t Data_Size);
void Modify_UART_Baudrate(tUART * UART, int32_t New_Baudrate);
void UART_Flush_TX(tUART * UART);


void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif // UART_UART_H_ 
