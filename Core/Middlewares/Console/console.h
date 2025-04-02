/*
 * console.h
 *
 *  Created on: Sep 22, 2024
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
    uint8_t TX_Buff[MAX_CONSOLE_BUFF_SIZE];
    uint32_t TX_Buff_Idx;
    uint8_t RX_Buff[MAX_CONSOLE_BUFF_SIZE];
    uint32_t RX_Buff_Idx;
    uint32_t RX_Task_Id;
    uint32_t TX_Task_Id;
    uint32_t Complete_Task_Id;
    void (*Complete_Task)(void *);
    eConsole_State Console_State;
    Queue * Console_Commands;
    Queue * Running_Repeat_Commands;

} tConsole;

void Quit_Commands(void * TaskHandle);
void Pause_Commands(void);


#ifdef __cplusplus
}
#endif

#endif /** CONSOLE_CONSOLE_H */
