/*
* UARTZephyr2.h
*
*  Created on: Sep 17, 2025
*      Author: buh07
*
*/
#ifndef UART_ZEPHYR2_H_
#define UART_ZEPHYR2_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../../Middlewares/Queue/queuezephyr.h"
#include "../../../../nordic/src/includes/zrtos.h"

#ifndef UART_RX_BUFF_SIZE
#define UART_RX_BUFF_SIZE 512
#endif
#ifndef MAX_TX_BUFF_SIZE
#define MAX_TX_BUFF_SIZE 2048
#endif

struct device;

typedef struct {
    uint8_t * Data;
    uint16_t Data_Size;
} TX_Node;

struct tUART_s;
typedef struct tUART_s tUART;

typedef struct {
    void (*SUDO_Transmit)(tUART *UART, uint8_t *Data, uint16_t Data_Size);
    void (*SUDO_Receive)(tUART *UART, uint8_t *Data, uint16_t *Data_Size);
} SUDO_UART;

typedef struct tUART_s {
    const struct device * UART_Handle;
    bool Use_DMA;
    bool UART_Enabled;

    z_pipe_t RX_Pipe;
    uint8_t * RX_Pipe_Storage;
    size_t RX_Pipe_Size;

    z_msgq_t TX_Queue;
    void * Queue_Storage;
    size_t Queue_Length;

    TX_Node * TX_Buffer;
    bool Currently_Transmitting;

    z_sem_t TX_Done_Sem;

    /* per-instance mutex to protect state (UART_Enabled, Currently_Transmitting, etc.) */
    z_mutex_t state_mutex;

    z_thread_t Thread;
    uint8_t Thread_Stack[512];
    size_t Thread_Stack_Size;

    SUDO_UART * SUDO_Handler;
} tUART;

void Init_UART_CallBack_Queue(void);
void Cleanup_UART_CallBack_Queue(void);
tUART * Init_DMA_UART(const struct device * uart_dev);
tUART * Init_SUDO_UART(void (*Transmit_Func_Ptr)(tUART*, uint8_t*, uint16_t), void (*Receive_Func_Ptr)(tUART*, uint8_t*, uint16_t*));
void Enable_UART(tUART * UART);
void Disable_UART(tUART * UART);
void UART_Delete(tUART * UART);
int8_t UART_Add_Transmit(tUART * UART, uint8_t * Data, uint16_t Data_Size);
void UART_Receive(tUART * UART, uint8_t * Data, uint16_t * Data_Size);
int8_t UART_SUDO_Receive(tUART * UART, uint8_t * Data, uint16_t * Data_Size);
void UART_Flush_TX(tUART * uart);
void Modify_UART_Baudrate(tUART * UART, int32_t New_Baudrate);