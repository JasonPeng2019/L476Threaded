/*
 * console.h
 *
 *  Created on: feb 22, 2025
 *      Author: jason.peng
 */

#ifndef CONSOLE_CONSOLE_H_
#define CONSOLE_CONSOLE_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "../../HAL/UART.h"
#include "../Queue/queue.h"


#ifdef __cplusplus
extern "C" {
#endif

#define MAX_CONSOLE_BUFF_SIZE		256
#define PRINTF_DELAY_TIME           100

typedef enum{
    eConsole_Wait_For_Commands = 0,
	eConsole_Servicing_Command,
    eConsole_Halting_Commands,
    eConsole_Halted_Commands,
    eConsole_Resume_Commands,
    eConsole_Quit_Commands,
} eConsole_State;

typedef enum{ 
    // look more at other console to find out difference between debug command, etc
    eConsole_Full_Command = 0,
    eConsole_Repeat_Command,
    eConsole_Debug_Command,
} eCommand_Type;

typedef struct {
    eCommand_Type Command_Type;
    const char * Command_Name;
    const char * Description;
    void (*Call_Function)(void *);
    void (*Halt_Function)(void *);
    void (*Resume_Function)(void *);
    void (*Stop_Function)(void *);
    void * Call_Params;
    void * Halt_Params;
    void * Resume_Params;
    void * Stop_Params;
    uint32_t Repeat_Time;
} tConsole_Command;

typedef struct {
    tUART * UART_Handler;
    uint8_t RX_Buff[MAX_CONSOLE_BUFF_SIZE];
    uint32_t RX_Buff_Idx;
    void (*Complete_Task)(void *);
    eConsole_State Console_State;
    Queue * Console_Commands;
    Queue * Running_Repeat_Commands;

} tConsole;

void printd(const char* format, ...);
void Quit_Commands(void);
void Resume_Commands(void);
void Pause_Commands(void);
static void Debug_Runner_Task(void * NULL_Ptr);
static void Process_Commands(uint8_t * data_ptr, uint16_t command_size);
static void RX_Task(void * NULL_Ptr);
void printd(const char* format, ...);
static void Clear_Screen(void);
tConsole_Command * Init_Debug_Command(const char * command_Name, const char * Description,
    void * Call_Function, void * Call_Params, void * Halt_Function, void * Halt_Params, 
    void * Resume_Function, void * Resume_Params, void * Stop_Function, void * Stop_Params);
void Init_Console(tUART * UART);

#ifdef __cplusplus
}
#endif

#endif /** CONSOLE_CONSOLE_H */
