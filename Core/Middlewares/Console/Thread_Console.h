/*
 * Thread_Console.h
 *
 *  Created on: Aug 15, 2025
 *      Author: claude.ai (ThreadX compliant version)
 */

#ifndef THREAD_CONSOLE_H_
#define THREAD_CONSOLE_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "../../Firmware/UART/UART.h"
#include "../Queue/queue.h"
#include "main.h"
#include "threadx_includes.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_CONSOLE_BUFF_SIZE           256
#define PRINTF_DELAY_TIME               100
#define CONSOLE_RX_SEMAPHORE_WAIT       TX_WAIT_FOREVER
#define CONSOLE_MUTEX_WAIT              100
#define CONSOLE_COMMAND_READY_FLAG      0x01
#define CONSOLE_THREAD_SLEEP_MS         1

typedef enum{
    eConsole_Wait_For_Commands = 0,
    eConsole_Servicing_Command,
    eConsole_Halting_Commands,
    eConsole_Halted_Commands,
    eConsole_Resume_Commands,
    eConsole_Quit_Commands,
} eConsole_State;

typedef enum{ 
    eConsole_Full_Command = 0,
    eConsole_Repeat_Command,
    eConsole_Debug_Command,
} eCommand_Type;

typedef struct {
    eCommand_Type Command_Type;
    char * Command_Name;
    char * Description;
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
    bool Complete_Need_Update;
    eConsole_State Console_State;
    Queue * Console_Commands;
    Queue * Running_Repeat_Commands;
} tConsole;

/* ThreadX Objects */
extern TX_THREAD rx_thread;
extern TX_THREAD debug_thread;
extern TX_THREAD complete_thread;
extern TX_MUTEX console_mutex;
extern TX_SEMAPHORE rx_semaphore;
extern TX_EVENT_FLAGS_GROUP console_events;

/* Public Functions */
void Thread_Console_Init(tUART * UART);
void Thread_Console_Shutdown(void);
tConsole_Command * Thread_Console_Add_Command(const char * command_Name, const char * Description, 
                                            void (*Call_Function)(void *), void * Call_Params);
tConsole_Command * Add_Console_Command(const char * command_Name, const char * Description, 
                                     void (*Call_Function)(void *), void * Call_Params);
tConsole_Command * Thread_Console_Add_Debug_Command(const char *command_Name,
                                                   const char *Description,
                                                   void (*Call_Function)(void *),
                                                   void *Call_Params,
                                                   void (*Halt_Function)(void *),
                                                   void *Halt_Params,
                                                   void (*Resume_Function)(void *),
                                                   void *Resume_Params,
                                                   void (*Stop_Function)(void *),
                                                   void *Stop_Params);

void thread_printd(const char* format, ...);
void Thread_Console_Pause_Commands(void);
void Thread_Console_Resume_Commands(void);
void Thread_Console_Quit_Commands(void);

/* Thread Entry Functions */
VOID RX_Thread_Entry(ULONG thread_input);
VOID Debug_Thread_Entry(ULONG thread_input);
VOID Complete_Thread_Entry(ULONG thread_input);

#ifdef __cplusplus
}
#endif

#endif /* THREAD_CONSOLE_H_ */