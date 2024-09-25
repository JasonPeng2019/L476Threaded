/*
 * console.c
 *
 *  Created on: Sep 22, 2024
 *      Author: jason.peng
 */


// note: to initialize a string, do A[] = "the quick brown fox jumped over the lazy dog"
// note: to send data that's been recieved into a buffer to something else via DMA, no CPU is needed 
// since it's from RAM to buffer. This means to print console commands, just "printd" the entire 
// buffer - it will be copied directly via memcpy to the data buffer of the queued up... wait...
// is this slower?
// no, for larger messages. for large messages: use DMA. change the pointer of "data" to the pointer of 
// "string".

#include <string.h>
#include <stdarg.h>
#include "console.h"
#include "main.h"
#include "stm32l476xx.h"
#include "stm32l4xx_hal.h"
#include "../Scheduler/scheduler.h"

static tConsole * console;
static void RX_Task(void * NULL_Ptr);
static void TX_Task(void * NULL_Ptr);
static void Handle_Rx(void);
static void Clear_Screen(void);

void Init_Console(tUART * UART){
    console->UART_Handler = UART;
    console->TX_Buff_Idx = 0;
    console->RX_Buff_Idx = 0;
    console->Console_State = eConsole_Wait_For_Commands;
    console->RX_Task_Id = Start_Task(RX_Task, NULL, 0);
    Set_Task_Name(console->RX_Task_Id, "CONSOLE_RX");
    Set_Task_Name(console->TX_Task_Id, "CONSOLE_TX");
    console->TX_Task_Id = Start_Task(TX_Task, NULL, 0);
    Prep_Queue(console->Console_Commands);
    Prep_Queue(console->Running_Repeat_Commands);

    Add_Console_Command("clear", "Clear the screen", Clear_Screen, NULL);
    printd("\r\nInput Command: \r\n");
    console->Complete_Task = Null_Task;
    console->Complete_Task_Id = Start_Task(console->Complete_Task, NULL, 0);
    Set_Task_Name(console->Complete_Task_Id, "CONSOLE_CMD");
}

tConsole_Command * Init_Reg_Command(const char * command_Name, const char * Description, void * Call_Function, void * Call_Params){
    tConsole_Command * new_Command = (tConsole_Command *)Task_Malloc_Data(console->Complete_Task_Id, sizeof(tConsole_Command));
    // if malloc successful:
        // malloc for the command->Command_Name
        //if malloc successful:
            //malloc for the description
            //if malloc successful
                //initialize the command name to the command name member
                //initialize description
                //initialize call function & init call params
                //assign NULL to stop and stop params
                //assign a repeat time of 0
}


tConsole_Command * Init_Debug_Command(const char * command_Name, const char * Description, void * Call_Function, void * Call_Params, void * Stop_Function, void * Stop_Params){
    //malloc new command
    // if malloc successful
        // malloc for the command->Command Name
        //if malloc successful
            // initialize the command name to the command name member
            //if malloc successful
                //initialize command name
                //initialize description
                //initialize call function & init call params
                //init stop function & stop function params
                //assign a repeat time
}

//DMA print
void printd(const char* format, ...) {
    va_list args;
    const char* percent_sign = strchr(format, '%');
    if (percent_sign == NULL) {
        UART_Add_Transmit(console->UART_Handler, format, strlen(format));
    } else {
        va_start(args, format);
        int needed_size = vsnprintf(NULL, 0, format, args) + 1;  // +1 for null terminator
        char* buffer = (char*)malloc(needed_size);
        if (buffer == NULL) {
            va_end(args); 
            return;       
        }
        vsnprintf(buffer, needed_size, format, args);
        va_end(args);
        UART_Add_Transmit(console->UART_Handler, buffer, strlen(buffer));
        free(buffer);
    }
}

//redirect printf to a blocking transmission
int __io_putchar(int ch) {
    // Send the character using HAL_UART_Transmit in blocking mode
    HAL_UART_Transmit(console->UART_Handler, (uint8_t*)&ch, 1, HAL_MAX_DELAY);  // Blocking call
    return ch;
}

static void RX_Task(void * NULL_Ptr){
    
}

