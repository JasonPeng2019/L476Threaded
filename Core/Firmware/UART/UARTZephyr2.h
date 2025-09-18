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

#ifndef UART_RX_BUFF_SIZE
#define UART_RX_BUFF_SIZE 512
#endif
#ifndef MAX_TX_BUFF_SIZE
#define MAX_TX_BUFF_SIZE 2048
#endif

struct k_pipe;
struct k_msgq;
struct k_sem;
struct k_thread;
struct device;
struct k_mutex;

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

    struct k_pipe RX_Pipe;
    uint8_t * RX_Pipe_Storage;
    size_t RX_Pipe_Size;

    struct k_msgq TX_Queue;
    void * Queue_Storage;
    size_t Queue_Length;

    TX_Node * TX_Buffer;
    volatile bool Currently_Transmitting;

    struct k_sem TX_Done_Sem;

    /* per-instance mutex to protect state (UART_Enabled, Currently_Transmitting, etc.) */
    struct k_mutex state_mutex;

    struct k_thread Thread;
    uint8_t Thread_Stack[512];
    size_t Thread_Stack_Size;

    SUDO_UART * SUDO_Handler;
} tUART;

void Init_UART_CallBack_Queue(void);
tUART * Init_DMA_UART(const struct device * uart_dev);
tUART * Init_SUDO_UART(void (*Transmit_Func_Ptr)(tUART*, uint8_t*, uint16_t), void (*Receive_Func_Ptr)(tUART*, uint8_t*, uint16_t*));
void Enable_UART(tUART * UART);
void Disable_UART(tUART * UART);
void UART_Delete(tUART * UART);
int8_t UART_Add_Transmit(tUART * UART, uint8_t * Data, uint8_t Data_Size);
void UART_Receive(tUART * UART, uint8_t * Data, uint16_t * Data_Size);