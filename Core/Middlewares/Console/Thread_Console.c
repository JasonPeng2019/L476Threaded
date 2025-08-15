/*
 * Thread_Console.c
 *
 *  Created on: Aug 15, 2025
 *      Author: claude.ai (ThreadX compliant version)
 */

#include <string.h>
#include <stdarg.h>
#include "Thread_Console.h"

static tConsole console_data;
static tConsole * console = &console_data;

/* ThreadX thread objects and stacks */
TX_THREAD rx_thread;
TX_THREAD debug_thread;
TX_THREAD complete_thread;
static UCHAR rx_thread_stack[TX_APP_THREAD_STACK_SIZE];
static UCHAR debug_thread_stack[TX_SMALL_APP_THREAD_STACK_SIZE];
static UCHAR complete_thread_stack[TX_SMALL_APP_THREAD_STACK_SIZE];

/* ThreadX synchronization objects */
TX_MUTEX console_mutex;
TX_SEMAPHORE rx_semaphore;
TX_EVENT_FLAGS_GROUP console_events;

/* RX buffer for DMA */
uint8_t data[UART_RX_BUFF_SIZE];
uint16_t data_size;

/* Static flags */
static bool RX_Buff_MAX_SURPASSED = false;

/* Private function declarations */
static void Process_Commands(uint8_t * data_ptr, uint16_t command_size);
static void Clear_Screen(void);
static void Null_Task(void * NULL_Ptr);
static UINT Safe_Block_Allocate(TX_BLOCK_POOL *pool, VOID **block_ptr, ULONG wait_option);
static UINT Safe_Block_Release(VOID *block_ptr);

void Thread_Console_Init(tUART * UART)
{
    UINT status;
    
    /* Initialize console data */
    console->UART_Handler = UART;
    console->RX_Buff_Idx = 0;
    console->Console_State = eConsole_Wait_For_Commands;
    console->Complete_Task = Null_Task;
    console->Complete_Need_Update = false;
    
    /* Create ThreadX synchronization objects */
    status = tx_mutex_create(&console_mutex, "CONSOLE_MUTEX", TX_INHERIT);
    if (status != TX_SUCCESS) {
        thread_printd("ERROR: Console mutex creation failed: %u\r\n", status);
        return;
    }
    
    
    status = tx_semaphore_create(&rx_semaphore, "RX_SEMAPHORE", 0);
    if (status != TX_SUCCESS) {
        thread_printd("ERROR: RX semaphore creation failed: %u\r\n", status);
        return;
    }
    
    status = tx_event_flags_create(&console_events, "CONSOLE_EVENTS");
    if (status != TX_SUCCESS) {
        thread_printd("ERROR: Console events creation failed: %u\r\n", status);
        return;
    }
    
    /* Initialize queues */
    console->Console_Commands = Prep_Queue();
    console->Running_Repeat_Commands = Prep_Queue();
    
    if (!console->Console_Commands || !console->Running_Repeat_Commands) {
        thread_printd("ERROR: Queue initialization failed\r\n");
        return;
    }
    
    /* Create threads with proper priorities */
    status = tx_thread_create(&rx_thread, "CONSOLE_RX", RX_Thread_Entry, 0,
                             rx_thread_stack, TX_APP_THREAD_STACK_SIZE,
                             3, 3, TX_NO_TIME_SLICE, TX_AUTO_START);
    if (status != TX_SUCCESS) {
        thread_printd("ERROR: RX thread creation failed: %u\r\n", status);
        return;
    }
    
    status = tx_thread_create(&debug_thread, "CONSOLE_DEBUG", Debug_Thread_Entry, 0,
                             debug_thread_stack, TX_SMALL_APP_THREAD_STACK_SIZE,
                             5, 5, TX_NO_TIME_SLICE, TX_AUTO_START);
    if (status != TX_SUCCESS) {
        thread_printd("ERROR: Debug thread creation failed: %u\r\n", status);
        return;
    }
    
    status = tx_thread_create(&complete_thread, "CONSOLE_CMD", Complete_Thread_Entry, 0,
                             complete_thread_stack, TX_SMALL_APP_THREAD_STACK_SIZE,
                             4, 4, TX_NO_TIME_SLICE, TX_AUTO_START);
    if (status != TX_SUCCESS) {
        thread_printd("ERROR: Complete thread creation failed: %u\r\n", status);
        return;
    }
    
    /* Add default commands */
    Thread_Console_Add_Command("clear", "Clear the screen", Clear_Screen, NULL);
    
    thread_printd("\r\nThreadX Console Initialized\r\nInput Command: \r\n");
}

void Thread_Console_Shutdown(void)
{
    /* Terminate threads */
    tx_thread_terminate(&rx_thread);
    tx_thread_terminate(&debug_thread);
    tx_thread_terminate(&complete_thread);
    
    /* Delete threads */
    tx_thread_delete(&rx_thread);
    tx_thread_delete(&debug_thread);
    tx_thread_delete(&complete_thread);
    
    /* Delete synchronization objects */
    tx_mutex_delete(&console_mutex);
    tx_semaphore_delete(&rx_semaphore);
    tx_event_flags_delete(&console_events);
}

tConsole_Command * Thread_Console_Add_Command(const char * command_Name, const char * Description,
                                            void (*Call_Function)(void *), void * Call_Params)
{
    tConsole_Command * new_Command = NULL;
    UINT status;
    
    /* Allocate command structure */
    status = Safe_Block_Allocate(&tx_app_mid_block_pool, (VOID **)&new_Command, TX_NO_WAIT);
    if (status != TX_SUCCESS) {
        thread_printd("ERROR: Command allocation failed\r\n");
        return NULL;
    }
    
    /* Allocate and copy command name */
    status = Safe_Block_Allocate(&tx_app_mid_block_pool, (VOID **)&new_Command->Command_Name, TX_NO_WAIT);
    if (status != TX_SUCCESS) {
        Safe_Block_Release(new_Command);
        thread_printd("ERROR: Command name allocation failed\r\n");
        return NULL;
    }
    strcpy(new_Command->Command_Name, command_Name);
    
    /* Allocate and copy description if provided */
    if (Description != NULL) {
        status = Safe_Block_Allocate(&tx_app_large_block_pool, (VOID **)&new_Command->Description, TX_NO_WAIT);
        if (status != TX_SUCCESS) {
            Safe_Block_Release(new_Command->Command_Name);
            Safe_Block_Release(new_Command);
            thread_printd("ERROR: Description allocation failed\r\n");
            return NULL;
        }
        strcpy(new_Command->Description, Description);
    } else {
        new_Command->Description = NULL;
    }
    
    /* Initialize command structure */
    new_Command->Command_Type = eConsole_Full_Command;
    new_Command->Call_Function = Call_Function;
    new_Command->Call_Params = Call_Params;
    new_Command->Resume_Function = NULL;
    new_Command->Resume_Params = NULL;
    new_Command->Halt_Function = NULL;
    new_Command->Halt_Params = NULL;
    new_Command->Stop_Function = NULL;
    new_Command->Stop_Params = NULL;
    new_Command->Repeat_Time = 0;
    
    /* Add to queue */
    if (!Enqueue(console->Console_Commands, new_Command)) {
        if (new_Command->Description) Safe_Block_Release(new_Command->Description);
        Safe_Block_Release(new_Command->Command_Name);
        Safe_Block_Release(new_Command);
        thread_printd("ERROR: Failed to add command to queue\r\n");
        return NULL;
    }
    
    return new_Command;
}

tConsole_Command * Thread_Console_Add_Debug_Command(const char *command_Name,
                                                   const char *Description,
                                                   void (*Call_Function)(void *),
                                                   void *Call_Params,
                                                   void (*Halt_Function)(void *),
                                                   void *Halt_Params,
                                                   void (*Resume_Function)(void *),
                                                   void *Resume_Params,
                                                   void (*Stop_Function)(void *),
                                                   void *Stop_Params)
{
    tConsole_Command *new_Command = NULL;
    UINT status;
    
    /* Allocate command structure */
    status = Safe_Block_Allocate(&tx_app_mid_block_pool, (VOID **)&new_Command, TX_NO_WAIT);
    if (status != TX_SUCCESS) {
        thread_printd("ERROR: Debug command allocation failed\r\n");
        return NULL;
    }
    
    /* Allocate and copy command name */
    status = Safe_Block_Allocate(&tx_app_mid_block_pool, (VOID **)&new_Command->Command_Name, TX_NO_WAIT);
    if (status != TX_SUCCESS) {
        Safe_Block_Release(new_Command);
        thread_printd("ERROR: Debug command name allocation failed\r\n");
        return NULL;
    }
    strcpy(new_Command->Command_Name, command_Name);
    
    /* Allocate and copy description if provided */
    if (Description) {
        status = Safe_Block_Allocate(&tx_app_large_block_pool, (VOID **)&new_Command->Description, TX_NO_WAIT);
        if (status != TX_SUCCESS) {
            Safe_Block_Release(new_Command->Command_Name);
            Safe_Block_Release(new_Command);
            thread_printd("ERROR: Debug description allocation failed\r\n");
            return NULL;
        }
        strcpy(new_Command->Description, Description);
    } else {
        new_Command->Description = NULL;
    }
    
    /* Initialize debug command structure */
    new_Command->Command_Type = eConsole_Debug_Command;
    new_Command->Call_Function = Call_Function;
    new_Command->Call_Params = Call_Params;
    new_Command->Halt_Function = Halt_Function;
    new_Command->Halt_Params = Halt_Params;
    new_Command->Resume_Function = Resume_Function;
    new_Command->Resume_Params = Resume_Params;
    new_Command->Stop_Function = Stop_Function;
    new_Command->Stop_Params = Stop_Params;
    new_Command->Repeat_Time = 50;  // Default 50ms
    
    /* Add to queue */
    if (!Enqueue(console->Console_Commands, new_Command)) {
        if (new_Command->Description) Safe_Block_Release(new_Command->Description);
        Safe_Block_Release(new_Command->Command_Name);
        Safe_Block_Release(new_Command);
        thread_printd("ERROR: Failed to add debug command to queue\r\n");
        return NULL;
    }
    
    return new_Command;
}

void thread_printd(const char* format, ...)
{
    va_list args;
    const char* percent_sign = strchr(format, '%');
    
    if (percent_sign == NULL) {
        /* Simple string, send directly */
        UART_Add_Transmit(console->UART_Handler, (uint8_t *)format, strlen(format));
    } else {
        /* Formatted string, use block pool instead of malloc */
        char* buffer = NULL;
        UINT status;
        
        va_start(args, format);
        int needed_size = vsnprintf(NULL, 0, format, args) + 1;
        va_end(args);
        
        /* Try to allocate from appropriate block pool */
        if (needed_size <= TX_APP_MID_BLOCK_SIZE) {
            status = Safe_Block_Allocate(&tx_app_mid_block_pool, (VOID **)&buffer, TX_NO_WAIT);
        } else if (needed_size <= TX_APP_LARGE_BLOCK_SIZE) {
            status = Safe_Block_Allocate(&tx_app_large_block_pool, (VOID **)&buffer, TX_NO_WAIT);
        } else {
            thread_printd("ERROR: Print buffer too large (%d bytes)\r\n", needed_size);
            return;
        }
        
        if (status != TX_SUCCESS || buffer == NULL) {
            thread_printd("ERROR: Print buffer allocation failed\r\n");
            return;
        }
        
        va_start(args, format);
        vsnprintf(buffer, needed_size, format, args);
        va_end(args);
        
        UART_Add_Transmit(console->UART_Handler, (uint8_t*)buffer, strlen(buffer));
        Safe_Block_Release(buffer);
    }
}

int __io_putchar(int ch)
{
    HAL_UART_Transmit(console->UART_Handler, (uint8_t*)&ch, 1, PRINTF_DELAY_TIME);
    return ch;
}

VOID RX_Thread_Entry(ULONG thread_input)
{
    (void)thread_input;
    UINT status;
    
    while (1) {
        /* Receive data from UART */
        UART_Receive(console->UART_Handler, data, &data_size);
        
        if (data_size > 0) {
            /* Acquire console mutex */
            status = tx_mutex_get(&console_mutex, CONSOLE_MUTEX_WAIT);
            if (status != TX_SUCCESS) {
                thread_printd("ERROR: Failed to acquire console mutex in RX\r\n");
                tx_thread_sleep(CONSOLE_THREAD_SLEEP_MS);
                continue;
            }
            
            /* Process received data */
            for (uint16_t counter = 0; counter < data_size; counter++) {
                /* Handle different console states */
                if (console->Console_State == eConsole_Halting_Commands) {
                    /* Stop all running commands */
                    for (int i = 0; i < console->Running_Repeat_Commands->Size; i++) {
                        tConsole_Command * curr_Command = (tConsole_Command*)Queue_Peek(console->Running_Repeat_Commands, i);
                        if (curr_Command && curr_Command->Stop_Function) {
                            curr_Command->Stop_Function(curr_Command->Stop_Params);
                        }
                    }
                    console->Console_State = eConsole_Halted_Commands;
                }
                
                /* Handle resume command when halted */
                if (console->Console_State == eConsole_Halted_Commands) {
                    if (counter >= 2 && data[counter - 2] == '!' && data[counter - 1] == 'r' && data[counter] == '\r') {
                        Thread_Console_Resume_Commands();
                    }
                }
                
                /* Handle normal command input */
                if (console->Console_State == eConsole_Wait_For_Commands || console->Console_State == eConsole_Halted_Commands) {
                    /* Check buffer overflow */
                    if (console->RX_Buff_Idx >= MAX_CONSOLE_BUFF_SIZE - 1) {
                        console->RX_Buff_Idx = 0;
                        RX_Buff_MAX_SURPASSED = true;
                    }
                    
                    /* Handle backspace */
                    if (data[counter] == '\b' || data[counter] == 0x7F) {
                        if (console->RX_Buff_Idx > 0) {
                            thread_printd("\b \b");
                            console->RX_Buff_Idx--;
                        }
                    }
                    /* Handle normal characters */
                    else if (RX_Buff_MAX_SURPASSED == false) {
                        console->RX_Buff[console->RX_Buff_Idx] = data[counter];
                        console->RX_Buff_Idx++;
                        thread_printd("%c", data[counter]);
                    }
                    
                    /* Handle enter key */
                    if (data[counter] == '\r') {
                        console->RX_Buff[console->RX_Buff_Idx - 1] = '\0';
                        thread_printd("\r\n");
                        
                        if (RX_Buff_MAX_SURPASSED) {
                            thread_printd("**COMMAND TOO LONG**\r\n");
                        } else {
                            Process_Commands(console->RX_Buff, console->RX_Buff_Idx);
                        }
                        
                        RX_Buff_MAX_SURPASSED = false;
                        console->RX_Buff_Idx = 0;
                    }
                }
                /* Handle servicing state */
                else if (console->Console_State == eConsole_Servicing_Command) {
                    if (data[counter] == '\r') {
                        thread_printd("Console paused.\r\n");
                        Thread_Console_Pause_Commands();
                    }
                }
                /* Handle quit state */
                else if (console->Console_State == eConsole_Quit_Commands) {
                    for (int i = 0; i < console->Running_Repeat_Commands->Size; i++) {
                        tConsole_Command * curr_command = Queue_Peek(console->Running_Repeat_Commands, i);
                        if (curr_command && curr_command->Stop_Function) {
                            curr_command->Stop_Function(curr_command->Stop_Params);
                        }
                    }
                    console->Console_State = eConsole_Wait_For_Commands;
                }
            }
            
            /* Release console mutex */
            tx_mutex_put(&console_mutex);
            
            /* Clear receive buffer */
            memset(data, 0, UART_RX_BUFF_SIZE);
            data_size = 0;
        }
        
        tx_thread_sleep(CONSOLE_THREAD_SLEEP_MS);
    }
}

VOID Debug_Thread_Entry(ULONG thread_input)
{
    (void)thread_input;
    
    while (1) {
        /* Execute all running debug commands - queue.c handles mutex protection */
        for (int i = 0; i < console->Running_Repeat_Commands->Size; i++) {
            tConsole_Command * curr_Command = (tConsole_Command *)Queue_Peek(console->Running_Repeat_Commands, i);
            if (curr_Command && curr_Command->Call_Function) {
                curr_Command->Call_Function(curr_Command->Call_Params);
            }
        }
        
        tx_thread_sleep(200); /* 200ms debug cycle */
    }
}

VOID Complete_Thread_Entry(ULONG thread_input)
{
    (void)thread_input;
    UINT status;
    
    while (1) {
        /* Acquire console mutex */
        status = tx_mutex_get(&console_mutex, CONSOLE_MUTEX_WAIT);
        if (status == TX_SUCCESS) {
            if (console->Complete_Task != NULL && console->Complete_Task != Null_Task) {
                console->Complete_Task(NULL);
            }
            tx_mutex_put(&console_mutex);
        }
        
        tx_thread_sleep(CONSOLE_THREAD_SLEEP_MS);
    }
}

static void Process_Commands(uint8_t * data_ptr, uint16_t command_size)
{
    char command[command_size];
    strcpy(command, (char *)data_ptr);
    
    char prompt_help_stop_halt[5] = {0};
    strncpy(prompt_help_stop_halt, command, 4);
    
    char prompt_resume[7] = {0};
    strncpy(prompt_resume, command, 6);
    
    bool halt_flag = (strcmp(prompt_help_stop_halt, "halt") == 0);
    bool stop_flag = (strcmp(prompt_help_stop_halt, "stop") == 0);
    bool help_flag = (strcmp(prompt_help_stop_halt, "help") == 0);
    bool resume_flag = (strcmp(prompt_resume, "resume") == 0);
    bool flag_3 = halt_flag || stop_flag || help_flag;
    
    /* Handle help command */
    if (strcmp(command, "help") == 0) {
        thread_printd("\r\n");
        /* queue.c handles mutex protection */
        for (int i = 0; i < console->Console_Commands->Size; i++) {
            tConsole_Command * curr_Command = (tConsole_Command *)Queue_Peek(console->Console_Commands, i);
            if (curr_Command) {
                thread_printd("%s: %s\r\n", curr_Command->Command_Name, 
                            curr_Command->Description ? curr_Command->Description : "No description");
            }
        }
    }
    /* Handle quit command */
    else if (strcmp(command, "quit") == 0) {
        thread_printd("Quitting commands.\r\n");
        Thread_Console_Quit_Commands();
    }
    /* Handle prefixed commands (halt/stop/help <command>) */
    else if (flag_3 && command_size > 5) {
        char prompt_command[command_size - 4];
        strcpy(prompt_command, command + 5); /* Skip "halt ", "stop ", "help " */
        
        /* queue.c handles mutex protection */
        for (int i = 0; i < console->Console_Commands->Size; i++) {
            tConsole_Command * curr_Command = (tConsole_Command *)Queue_Peek(console->Console_Commands, i);
            if (curr_Command && strcmp(prompt_command, curr_Command->Command_Name) == 0) {
                if (halt_flag && curr_Command->Halt_Function) {
                    curr_Command->Halt_Function(curr_Command->Halt_Params);
                }
                if (stop_flag && curr_Command->Stop_Function) {
                    curr_Command->Stop_Function(curr_Command->Stop_Params);
                    Thread_Console_Quit_Commands();
                }
                if (help_flag) {
                    thread_printd("%s: %s\r\n", curr_Command->Command_Name, 
                                curr_Command->Description ? curr_Command->Description : "No description");
                }
                break;
            }
        }
    }
    /* Handle resume command */
    else if (resume_flag && command_size > 7) {
        char prompt_command[command_size - 6];
        strcpy(prompt_command, command + 7); /* Skip "resume " */
        
        /* queue.c handles mutex protection */
        for (int i = 0; i < console->Console_Commands->Size; i++) {
            tConsole_Command * curr_Command = (tConsole_Command *)Queue_Peek(console->Console_Commands, i);
            if (curr_Command && strcmp(prompt_command, curr_Command->Command_Name) == 0) {
                if (curr_Command->Resume_Function) {
                    curr_Command->Resume_Function(curr_Command->Resume_Params);
                }
                break;
            }
        }
    }
    /* Handle regular commands */
    else {
        /* queue.c handles mutex protection */
        for (int i = 0; i < console->Console_Commands->Size; i++) {
            tConsole_Command * curr_Command = (tConsole_Command *)Queue_Peek(console->Console_Commands, i);
            if (curr_Command && strcmp(command, curr_Command->Command_Name) == 0) {
                /* Check if command is already running */
                bool command_already_running = false;
                for (int c = 0; c < console->Running_Repeat_Commands->Size; c++) {
                    tConsole_Command * curr_Running_Command = (tConsole_Command *)Queue_Peek(console->Running_Repeat_Commands, c);
                    if (curr_Running_Command && strcmp(command, curr_Running_Command->Command_Name) == 0) {
                        thread_printd("Command Already Running\r\n");
                        command_already_running = true;
                        break;
                    }
                }
                
                if (!command_already_running && curr_Command->Call_Function) {
                    curr_Command->Call_Function(curr_Command->Call_Params);
                    thread_printd("Starting %s command.\r\n", curr_Command->Command_Name);
                    
                    /* Add debug commands to running list */
                    if (curr_Command->Command_Type == eConsole_Debug_Command) {
                        Enqueue(console->Running_Repeat_Commands, curr_Command);
                    }
                }
                break;
            }
        }
    }
}

static void Clear_Screen(void)
{
    thread_printd("\033[2J");
    thread_printd("%c[2j%c[H", 27, 27);
}

void Thread_Console_Pause_Commands(void)
{
    UINT status = tx_mutex_get(&console_mutex, CONSOLE_MUTEX_WAIT);
    if (status == TX_SUCCESS) {
        console->Console_State = eConsole_Halting_Commands;
        tx_mutex_put(&console_mutex);
    }
}

void Thread_Console_Quit_Commands(void)
{
    UINT status = tx_mutex_get(&console_mutex, CONSOLE_MUTEX_WAIT);
    if (status == TX_SUCCESS) {
        console->Console_State = eConsole_Quit_Commands;
        tx_mutex_put(&console_mutex);
    }
}

void Thread_Console_Resume_Commands(void)
{
    UINT status = tx_mutex_get(&console_mutex, CONSOLE_MUTEX_WAIT);
    if (status == TX_SUCCESS) {
        console->Console_State = eConsole_Resume_Commands;
        tx_mutex_put(&console_mutex);
    }
}

static void Null_Task(void * NULL_Ptr)
{
    (void)NULL_Ptr;
} 


