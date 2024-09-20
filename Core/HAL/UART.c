/*
 * UART.h
 *
 *  Created on: Sep 19, 2024
 *      Author: jason.peng
 */
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "UART.h"
#include "../Middlewares/Scheduler/Scheduler.h"
#include "main.h"


static bool UART_Callbacks_Initialized = false;
static Queue * UART_Callback_Handles;
static void UART_Handler(void * Task_Data);

void Init_CallBack_Queue(Queue * Callback_Queue){
    Prep_Queue(Callback_Queue);
}

tUART * Init_DMA_UART(UART_HandleTypeDef * UART_Handle){
    //malloc a UART struct
    tUART * UART = (tUART*)malloc(sizeof(tUART));
    //initialize all the members
    if (UART != NULL){
        UART->UART_Handle = UART_Handle;
        UART->Use_DMA = true;
        UART->UART_Enabled = true;
        UART->TX_Buffer = NULL; //tracker to tell if transmission has happened before (flag)
        UART->Currently_Transmitting = false;
        UART->RX_Buff_Head_Idx = 0;
        UART->RX_Buff_Tail_Idx = 0;
        UART->SUDO_Handler = NULL;
        Prep_Queue(UART->TX_Queue);
        
        //enqueue it to the callback handles so we can find it when we need to do callbacks
        Enqeueue(UART_Callback_Handles, (void *)UART);
        //start a new task for checking this UART and handling it
        UART->Task_ID = Start_Task(UART_Handler, (void*)UART, 0);  // timeout value is 0. that means 
                                                // will always proc for new UART TX's. (Immediately)

        //add the UART memory to the memory for the task (right now only holds task structure)
        Task_Add_Heap_Usage(UART->Task_ID, (void*)UART);
        Set_Task_Name(UART->Task_ID, "UART RX/TX");
        //set up the DMA access
        HAL_UART_Receive_DMA(UART_Handle, UART->RX_Buffer, UART_RX_BUFF_SIZE);
        //return a pointer to the struct

    } else {
        printf("func INIT UART: malloc failed");
    }
}

tUART * Init_SUDO_UART(void * (*Transmit_Func_Ptr)(uint8_t*, uint8_t), void * (*Receive_Func_Ptr)(uint8_t*, uint8_t)){
    tUART * UART = (tUART*)malloc(sizeof(tUART));
    if (UART != NULL){
        UART->UART_Handle = NULL;
        UART->Use_DMA = false;
        UART->UART_Enabled = true;
        UART->TX_Buffer = NULL;
        UART->Currently_Transmitting = false;
        UART->RX_Buff_Head_Idx = 0;
        UART->RX_Buff_Tail_Idx = 0;
        UART->SUDO_Handler->SUDO_Transmit = Transmit_Func_Ptr;
        UART->SUDO_Handler->SUDO_Receive = Receive_Func_Ptr;
        Prep_Queue(UART->TX_Queue);
        UART->Task_ID = Start_Task(UART_Handler, (void*)UART, 0);
        Task_Add_Heap_Usage(UART->Task_ID, (void*)UART);
        Set_Task_Name(UART->Task_ID, "SUDO UART RX/TX");
    } else {
        printf("func INIT UART: malloc failed");
    }    
}

/* handle the TX in the queue accordingly
 * don't need to handle Rx because Rx is polled by tasks running UART (example - polled by console
 * if console is in use)
 * If device is active, then UART is being polled and continuously in use.
 * No long idle periods for UART - program the device and the UART to recieve intermittently
 * 
 */
void UART_Handler(void * UART_tVoid){
    tUART * UART = (tUART*)UART_tVoid;
    // if ready to transmit
    if (!UART->Currently_Transmitting && UART->UART_Enabled && UART->TX_Queue->Size > 0){
        // clear out the previous buffer
        if (UART->TX_Buffer != NULL){
            Task_Free(UART->Task_ID, UART->TX_Buffer->Data); // can do this because malloc remembers size, when you malloc'd 
            Task_Free(UART->Task_ID, UART->TX_Buffer);       // even though address is only the first element
        }
        // dequeue a Tx from the Tx_Queue
        UART->TX_Buffer = Dequeue(UART->TX_Queue); // don't need to add it to the Queue memory since the data in queue already is in
        // transmit it, and then block the UART from transmitting until ready (Tx Callback makes it ready)
        if(UART->Use_DMA){
			HAL_UART_Transmit_DMA(UART->UART_Handle, UART->TX_Buffer->Data, UART->TX_Buffer->Data_Size);
        } else if (UART->SUDO_Handler != NULL){
            UART->SUDO_Handler->SUDO_Transmit(UART->UART_Handle, UART->TX_Buffer->Data, UART->TX_Buffer->Data_Size);
        }
        UART->Currently_Transmitting = true;
    }
} 

void Enable_UART(tUART * UART){
	HAL_UART_MspInit(UART->UART_Handle);
	Prep_Queue(&UART->TX_Queue);
	UART->TX_Buffer = NULL;
	UART->Currently_Transmitting = false;
	UART->RX_Buff_Head_Idx = 0;
	UART->RX_Buff_Tail_Idx = 0;
	UART->UART_Enabled = true;

	HAL_UART_Receive_DMA(UART->UART_Handle, UART->RX_Buffer, UART_RX_BUFF_SIZE);
}

void Disable_UART(tUART * UART){
    UART_Flush_TX(UART);
    // if using DMA
    if (UART->Use_DMA = true){
        if (UART->UART_Handle->RxState == HAL_UART_STATE_BUSY_RX){
            // allow all recieving transmissions to go thru
            printf("func Disable UART: Rx is busy, waiting for RX transmission to finish.\r\n");
            while (UART->UART_Handle->RxState == HAL_UART_STATE_BUSY_RX){
                HAL_Delay(1);
            }
            // then deactivate the UART DMA immeadiately
            HAL_UART_DMAStop(UART->UART_Handle);  // Stop the DMA reception
            printf("func Disable UART: Rx finished. Disabling UART\r\n");
        }
        // Deinit the UART
        HAL_UART_MspDeInit(UART->UART_Handle);
    }
    // free the TX Queue and TX Buffer and update the heap usage tracking variable accordingly
    int c = 0;
    for (; c < UART->TX_Queue->Size; c++){
        Task_Rm_Heap_Usage(UART->Task_ID, Free_Queue(UART->TX_Queue));
    }
    Task_Free(UART->Task_ID, UART->TX_Buffer->Data);
    Task_Free(UART->Task_ID, UART->TX_Buffer);
    //Update currently transmitting and UART Enabled
    UART->Currently_Transmitting = false;
    UART->UART_Enabled = false;
}



void Disable_UART(tUART * Bus);
int8_t UART_Add_Transmit(tUART * UART, uint8_t * Data, uint8_t Data_Size);
int8_t UART_Recieve(tUART * UART, uint8_t * Data, uint8_t Data_Size);
void Modify_UART_Baudrate(tUART * UART, int32_t New_Baudrate);
void UART_Flush_TX(tUART * UART);
