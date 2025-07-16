/*
 * UART.c
 *
 *  Created on: Feb 19, 2025
 *      Author: jason.peng
 */
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include "UART.h"
#include "../../Middlewares/Scheduler/Scheduler.h"
#include "main.h"

static bool UART_Callbacks_Initialized = false;
static Queue * UART_Callback_Handles;
static void UART_Task(tUART * UART);

void Init_UART_CallBack_Queue(void){
    UART_Callback_Handles = Prep_Queue();
}


/** 
 *@brief: malloc a UART, and initialize UART struct members for a UART using DMA. Add it to the callback
 * handles queue for when you want to do a callback to match it, then start a task for transmitting the 
 * UART (checking bufer and transmitting accordingly). Updates memory trackers for the UART task, and 
 * sets up the DMA Access. DMA never needs to be RX'd - it will automatically load into UART RX
 *
 * @params: UART_Handle returned from STM32 HAL.
 * 
 * @returns: tUART of UART initialized
*/
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
        UART->TX_Queue = Prep_Queue();
        
        //enqueue it to the callback handles so we can find it when we need to do callbacks
        Enqueue(UART_Callback_Handles, (void *)UART);
        //start a new task for checking this UART and handling it
        UART->Task_ID = Start_Task(UART_Task, (void*)UART, 0);  // timeout value is 0. that means 
                                                // will always proc for new UART TX's. (Immediately)
        //add the UART memory to the memory for the task (right now only holds task structure)

        tUART_Repeat_Receive * UART_Repeat_Handle = Task_Malloc_Data(UART->Task_ID, sizeof(tUART_Repeat_Receive));
        UART_Repeat_Handle->UART = UART;
        UART_Repeat_Handle->Repeat_Queue = Prep_Queue();
        UART_Repeat_Handle->Task_ID = Start_Task(UART_Repeat_RX_Task, UART_Repeat_Handle, 100);

        Task_Add_Heap_Usage(UART->Task_ID, (void*)UART);
        Set_Task_Name(UART->Task_ID, "UART RX/TX");
        //set up the DMA access
        HAL_UART_Receive_DMA(UART_Handle, UART->RX_Buffer, UART_RX_BUFF_SIZE);
        

        // init recieve function
        //return a pointer to the struct
        return UART;
    } else {
        printf("func INIT UART: malloc failed");
    }
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
tUART * Init_SUDO_UART(void * (*Transmit_Func_Ptr)(uint8_t*, uint8_t), void * (*Receive_Func_Ptr)(uint8_t*, uint8_t)){
    tUART * UART = (tUART*)malloc(sizeof(tUART));
    if (UART != NULL){
        UART->UART_Handle = NULL;
        UART->Use_DMA = false;
        UART->UART_Enabled = true;
        UART->TX_Buffer = NULL;
        UART->Currently_Transmitting = false;
        UART->Repeat_Handle = NULL;
        UART->RX_Buff_Head_Idx = 0;
        UART->RX_Buff_Tail_Idx = 0;
        UART->SUDO_Handler->SUDO_Transmit = Transmit_Func_Ptr;
        UART->SUDO_Handler->SUDO_Receive = Receive_Func_Ptr;
        UART->TX_Queue = Prep_Queue();
        UART->Task_ID = Start_Task(UART_Task, (void*)UART, 0);

        tUART_Repeat_Receive * UART_Repeat_Handle = Task_Malloc_Data(UART->Task_ID, sizeof(tUART_Repeat_Receive));
        UART_Repeat_Handle->UART = UART;
        UART_Repeat_Handle->Repeat_Queue = Prep_Queue();
        UART_Repeat_Handle->Task_ID = Start_Task(UART_Repeat_RX_Task, UART_Repeat_Handle, 100);

        Task_Add_Heap_Usage(UART->Task_ID, (void*)UART);
        Set_Task_Name(UART->Task_ID, "SUDO UART RX/TX");
    } else {
        printf("func INIT UART: malloc failed");
    }    
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
void UART_Task(tUART * UART){
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

/**
 * @brief: Enables UART from disable mode. Reinitializes the queue, reinitializes TX Buffer 
 * enables Recieve DMA.

 * @params: UART struct of UART being enabled
 * 
 * @return: None
 */
void Enable_UART(tUART * UART){
	HAL_UART_MspInit(UART->UART_Handle);
	UART->TX_Queue = Prep_Queue();
	UART->TX_Buffer = NULL;
	UART->Currently_Transmitting = false;
	UART->RX_Buff_Head_Idx = 0;
	UART->RX_Buff_Tail_Idx = 0;
	UART->UART_Enabled = true;

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
    TX_Node * to_Node = (TX_Node*)Task_Malloc_Data(UART->Task_ID, sizeof(TX_Node));
        // if malloc is successful:
    if (to_Node != NULL){
        // malloc for the data
        uint8_t * data_To_Add =  (uint8_t*)Task_Malloc_Data(UART->Task_ID, sizeof(uint8_t)*Data_Size);
        // if malloc for the data is successful:
        if (data_To_Add != NULL){
        // copy the data over to the node and enqueue the data:
            memcpy(data_To_Add, Data, Data_Size);
            to_Node->Data = data_To_Add;
            Enqueue(UART->TX_Queue, to_Node);
            return Data_Size;
        }
    }
    // or else, free the node and pointer (if malloc for data wasn't successful)
    free(to_Node);
    printf("func UART_Add_Trasmit: malloc error.\r\n");
    // return fail
    return -1;
}

/**
 * @brief: Recieves UART Data to the uint8_t data pointer from the Rx Buffer. If UART is not enabled, does not
 * do any recieving and returns false. If UART is busy, requeues the data entry and 
 * tries to recieve it again to the data buffer. Returns false anyways, but requeues 
 * so that the data is not lost.
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
int8_t UART_Receive(tUART * UART, uint8_t * Data, uint8_t * Data_Size, bool * External_Success_Flag){
 // if busy
    *Data_Size = 0;

    if (!UART->UART_Enabled){
        return 0;
    }

    if (UART->UART_Handle->RxState == HAL_UART_STATE_BUSY_RX){
        *External_Success_Flag = false;
        uint8_t * Data_Holder = (uint8_t *)Task_Malloc_Data(UART->Task_ID, Data_Size);
        uint8_t * Data_Size_Holder = (uint8_t *)Task_Malloc_Data(UART->Task_ID, sizeof(*Data_Size));
        memcpy(Data_Holder, Data, Data_Size);
        memcpy(Data_Size_Holder, Data_Size, sizeof(*Data_Size));
        if (!(UART_Repeat_Receive_Enqueue(UART, Data_Holder, Data_Size_Holder, External_Success_Flag))){
            printd("Malloc error for UART Receive.");
        };
        // do not need to worry about Data and Data_Size being dangling pointers because they are STATIC buffers
        return 0;
    } else {
        while (UART->RX_Buff_Tail_Idx != (UART_RX_BUFF_SIZE - UART->UART_Handle->hdmarx->Instance->CNDTR)){
            Data[*Data_Size++] = UART->RX_Buffer[UART->RX_Buff_Tail_Idx++];
            if (UART->RX_Buff_Tail_Idx >= UART_RX_BUFF_SIZE){
                UART->RX_Buff_Tail_Idx = 0;
            }
        }
    }
    return *Data_Size;
}

/**
 * @brief: The Repeating RX Task that repeats the reading process for
 *  any data that was unsuccessfully. 
 *  Peeks at each node in the Repeat_Queue, sees if the "done" flag is
 *  true (basically transmitted successfully). If done, does not transmit
 *  for that recieve node, but rather, gets rid of the node and its data, which
 *  is a tUART_Repeat_Node that holds pointers to various nodes.
 *  Otherwise, receives the signal to the buffer at the Repeat_Queue node.
 * 
 * @params: tUART_Repeat_Receive * struct to hold the Repeat Queue
 * 
 * @return: None
 * 
 */

void UART_Repeat_RX_Task(tUART_Repeat_Receive * UART_Repeat_Handle){
    Node * curr_Repeat_Node = Queue_Node_Peek(UART_Repeat_Handle->Repeat_Queue, 0);
    for (int i = 0; i < UART_Repeat_Handle->Repeat_Queue->Size; i++){
        if (i == 0 && ((tUART_Repeat_Node*)(curr_Repeat_Node->Data))->done == true){
            Task_Free(UART_Repeat_Handle->Task_ID, ((tUART_Repeat_Node*)(curr_Repeat_Node->Data))->success_Buff);
            free(curr_Repeat_Node->Data);
            curr_Repeat_Node->Next = NULL;
            free(curr_Repeat_Node);
        }
        Node * prev = curr_Repeat_Node;
        // make prev go to the left
        curr_Repeat_Node = Queue_Peek(UART_Repeat_Handle->Repeat_Queue, i);
        if (i != 0 && ((tUART_Repeat_Node*)(curr_Repeat_Node->Data))->done == true){
            prev->Next = curr_Repeat_Node->Next;
            Task_Free(UART_Repeat_Handle->Task_ID, ((tUART_Repeat_Node*)(curr_Repeat_Node->Data))->success_Buff);
            free(curr_Repeat_Node->Data);
            curr_Repeat_Node->Next = NULL;
            free(curr_Repeat_Node);
        } else {
            tUART_Repeat_Node * curr_Repeat_Handle = (tUART_Repeat_Node *)curr_Repeat_Node->Data;
            UART_Repeat_Receive(UART_Repeat_Handle->UART, curr_Repeat_Handle->Data_Buff,
            curr_Repeat_Handle->Data_Size_Buff, curr_Repeat_Handle->success_Buff);
        bool success_Flag = *(curr_Repeat_Handle->success_Buff);
        if (success_Flag) {
            curr_Repeat_Handle->done = true;
            }     
        }
    }
}

/**
 * @brief: Same as UART_Receive, except in the case of a busy transmit, does not enqueue the recieve buffer. 
 * Does not receive or move the receive index.
 * 
 * @params: tUART * UART Handle
 * @params: uint8_t * Data buffer to store received data
 * @params: uint8_t * Ptr to buffer to hold the size of data received.
 * @params: bool * Ptr to buffer to hold if transmit was successful or not. Need this because we don't pass 
 * the buffer holding the success buff, only the data and UART.
 * 
 * @return Size of Data Received.
 * 
 */
int8_t UART_Repeat_Receive(tUART * UART, uint8_t * Data, uint8_t * Data_Size, bool * Suxx_Buff){
    *Data_Size = 0;

    if (!UART->UART_Enabled){
        return 0;
    }

    if (!(UART->UART_Handle->RxState == HAL_UART_STATE_BUSY_RX)){
        while (UART->RX_Buff_Tail_Idx != (UART_RX_BUFF_SIZE - UART->UART_Handle->hdmarx->Instance->CNDTR)){
            Data[*Data_Size++] = UART->RX_Buffer[UART->RX_Buff_Tail_Idx++];
            if (UART->RX_Buff_Tail_Idx >= UART_RX_BUFF_SIZE){
                UART->RX_Buff_Tail_Idx = 0;
            }
            if (*Data_Size != 0){
                *Suxx_Buff = true;
            }
        }
    }
    return *Data_Size;
}

/*returns 1 if successful, 0 if otherwise*/
/**
 * @brief: Enqueues a new Repeat Node in the Repeat Handle (which )
 */
uint8_t UART_Repeat_Receive_Enqueue(tUART * UART, uint8_t * Data, uint8_t * Data_Size, bool * UART_RX_Flag){
 // get UART to have its own repeat HAndle
 // enqueue's to that UART's repeat Handle
 tUART_Repeat_Receive * Repeat_Handle = UART->Repeat_Handle;
 tUART_Repeat_Node * new_Repeat_Node = (tUART_Repeat_Node*)(UART->Task_ID, sizeof(tUART_Repeat_Node));
 new_Repeat_Node->Data_Buff = Data;
 new_Repeat_Node->Data_Size_Buff = Data_Size;
 new_Repeat_Node->success_Buff = UART_RX_Flag;
 new_Repeat_Node->done = false;
 return Enqueue(Repeat_Handle->Repeat_Queue, new_Repeat_Node);
}

int8_t UART_SUDO_Recieve(tUART * UART, uint8_t * Data, uint8_t Data_Size){
    return UART->SUDO_Handler->SUDO_Receive(UART, Data, Data_Size);
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
	// Only transmit if the UART is enabled
	if(!uart->UART_Enabled)
		return;

	while(uart->TX_Queue->Size != 0 || uart->Currently_Transmitting)
	{
		UART_Tasks((void *)uart);
	}
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
	// Find who the callback is for
	int c = 0;
	for(; c < UART_Callback_Handles->Size; c++)
	{
		tUART * uart = (tUART *)Queue_Peek(&UART_Callback_Handles, c);

		if(uart->UART_Handle == huart)
		{
			uart->Currently_Transmitting = false;
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
	int c = 0;
	for(; c < UART_Callback_Handles->Size; c++)
	{
		tUART * uart = (tUART *)Queue_Peek(&UART_Callback_Handles, c);

		if(uart->UART_Handle == huart)
		{
			uart->RX_Buff_Tail_Idx = 0;
			uart->Currently_Transmitting = false;
			HAL_DMA_Abort_IT(uart->UART_Handle->hdmarx);
			HAL_UART_DMAStop(uart->UART_Handle);
			HAL_UART_Receive_DMA(uart->UART_Handle, uart->RX_Buffer, UART_RX_BUFF_SIZE);
		}
	}
}
