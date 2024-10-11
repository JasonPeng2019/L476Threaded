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
static void Process_Command(void);
static void Clear_Screen(void);

void Init_Console(tUART * UART){
    console->UART_Handler = UART;
    console->TX_Buff_Idx = 0;
    console->RX_Buff_Idx = 0;
    console->Console_State = eConsole_Wait_For_Commands;
    console->RX_Task_Id = Start_Task(RX_Task, NULL, 0);
    Set_Task_Name(console->RX_Task_Id, "CONSOLE_RX");
    Set_Task_Name(console->TX_Task_Id, "CONSOLE_TX"); // instead of transmitting constantly, 
    console->TX_Task_Id = Start_Task(TX_Task, NULL, 0);
    Prep_Queue(console->Console_Commands);
    Prep_Queue(console->Running_Repeat_Commands);

    Add_Console_Command("clear", "Clear the screen", Clear_Screen, NULL);
    printd("\r\nInput Command: \r\n");
    console->Complete_Task = Null_Task;
    // since console->Complete_Task is not a repeating task, don't need to exe anything
    console->Complete_Task_Id = Start_Task(console->Complete_Task, NULL, 0);
    Set_Task_Name(console->Complete_Task_Id, "CONSOLE_CMD");
} 
// one console to handle all the full commands, each additional call is a new console
// new console killed after debug task ended
// one main console to handle all the tasks RX, TX, full tasks, and keep track of main queues (Console commands to execute, )

tConsole_Command * Init_Reg_Command(const char * command_Name, const char * Description, void * Call_Function, void * Call_Params){
    tConsole_Command * new_Command = (tConsole_Command *)Task_Malloc_Data(console->Complete_Task_Id, sizeof(tConsole_Command));
    // if malloc successful:
    if(new_Command != NULL){
        // malloc for the command->Command_Name
        new_Command->Command_Name = (const char *)Task_Malloc_Data(console->Complete_Task_Id, sizeof(char)*(strlen(command_Name)+1));
        strcpy(new_Command->Command_Name, command_Name);
        //if malloc successful:
        if (new_Command->Command_Name != NULL){
        //malloc for the description
            new_Command->Description = (const char *)Task_Malloc_Data(console->Complete_Task_Id, sizeof(char)*(strlen(Description)+1));
            strcpy(new_Command->Description, Description);
            //initialize call function & init call params
            new_Command->Call_Function = Call_Function;
            new_Command->Call_Params = Call_Params;
            //assign NULL to resume and stop call & params
            new_Command->Resume_Function = NULL;
            new_Command->Resume_Params = NULL;
            //assign a repeat time of 0
            new_Command->Repeat_Time = 0;
            // add to command_queue in console
            Enqeueue(console->Console_Commands, new_Command);
        }
    }
}


tConsole_Command * Init_Debug_Command(const char * command_Name, const char * Description,
    void * Call_Function, void * Call_Params, void * Resume_Function, void * Resume_Params){
    //malloc new command
    tConsole_Command * new_Command = (tConsole_Command *)Task_Malloc_Data(console->Complete_Task_Id, sizeof(tConsole_Command));
    // if malloc successful
    if (new_Command != NULL){
        // malloc for the command->Command Name
        new_Command->Command_Name = (const char *)Task_Malloc_Data(console->Complete_Task_Id, sizeof(char)*strlen(command_Name));
        strcpy(new_Command->Command_Name, command_Name);
        //if malloc successful
        if (new_Command->Command_Name != NULL){
            //initialize description
            new_Command->Description = (const char *)Task_Malloc_Data(console->Complete_Task_Id, sizeof(char)*strlen(Description));
            strcpy(new_Command->Description, Description);
            //initialize call function & init call params
            new_Command->Call_Function = Call_Function;
            new_Command->Call_Params = Call_Params;
            //init resume/stop function & resume/stop function params
            new_Command->Resume_Function = Resume_Function;
            new_Command->Resume_Params = Resume_Params;
            //assign a repeat time
            new_Command->Repeat_Time = 50; // 50 ms
            // add to commmand_queue in console
            Enqueue(console->Console_Commands, new_Command);
        }
    }
}

//flush TX data
static void Flush_TX_Data(void * Task_Data){
    fflush(stdout);
    if(console->TX_Buff_Idx > 0){
        UART_Add_Transmit(console->UART_Handler, console->TX_Buff, console->TX_Buff_Idx);
        console->TX_Buff_Idx = 0;
    }
}

//clear screen
static void Clear_Screen(void * Data){
	printf("\033[2J");
	printf("%c[2j%c[H",27,27);
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
    //malloc a buffer to recieve the DMA transmission -. can't use backspace cus then can't match to stuff like \b or \r
    uint8_t data[UART_RX_BUFF_SIZE];
    //initialize an int to hold the size for later storage
    uint16_t data_size;
    //recieve the data into the buffer
    UART_Receive(console->UART_Handler, data, data_size);
    //go thru the buffer using a counter to the data_size
    for (uint16_t counter = 0; counter < data_size; counter++){
    //if waiting for commands: can put in data
        if (console->Console_State == eConsole_Wait_For_Commands) {
        //if backspace: transmit a backspace and decrement the index of the UART buffer (since want to overwrite, go backwards one)
            if (data[counter] == '\b' || data[counter] == '0x7F'){
                if (console->RX_Buff_Idx > 0){
                    printf("\b \b");
                    
                }
            }
        //else store the next char in the RX buff.

        //if we get a return, null terminate the buff, print a new line, then store the command and process_command()
        //then clear the command buffer
        //else echo the command:
        //if command is too long, (clear buffer)

        //if stop, initialize a stop buffer
        // stop: get rid of the halt, add the command back to the command list, and run stop command.

        //handle the command: if in halted, then call resume if enter,
            }
        }
    // else if not waiting for commands, must be in servicing mode
        // if return: pause all commands, halt all the tasks, and change to halted. change 
        // console to waiting for commands. 

        //if halted: remove the command from command list, then add halt to the command list.
    }
}


static void Process_Command(void){
    // if strcmp help
    // for every command in console.commands_queue, peek at the command
    // switch: if command name is equal to input:
    // call function: check command state and call according command.
    // types of command inputs: A) FULL B) HALT (enter) C) RESUME 
    // if full/start command: call command (call function)
    // if repeat/debug command: check the console for a task that matches.
    // if none, start a new console and task, do all the assigning, etc. use task_malloc.
    // if in, do nothing and print that task is already running.
    // if return: check status: if servicing or halted.
    // if servicing: halt commands in console's running command queue
    // if halted: call resume functions on paused running command queue
    // "smag" should be a full command (stop mag = smag)
    // 
    // Leetcode Premium
}

// add command: just adds the name of a command to the queue, but when runs, use a different command.
// -> COMMANDS are added to the command queue (not running), and console tasks are added when command is called
// for a repeat command. Command is added to running list Queue (pointer, so same command), and all are halted 
// when halt is needed. if stop is needed, same thing
// console commands are 

//handle task: if a debug command / repeat command, need to make a new task. if stop, get rid of the task

//flush TX 

