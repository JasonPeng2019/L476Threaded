/*
 * Thread_Console.c
 *
 *  Created on: Aug 15, 2025
 *      Author: jason.peng (ThreadX compliant version)
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
TX_EVENT_FLAGS_GROUP console_events;

/* RX buffer for DMA */
uint8_t data[UART_RX_BUFF_SIZE];
uint16_t data_size;

/* Static flags */
static bool RX_Buff_MAX_SURPASSED = false;

/* Private function declarations */
static void Process_Commands(uint8_t * data_ptr, uint16_t command_size);
static void Clear_Screen(void * unused);
static void Null_Task(void * NULL_Ptr);


void Thread_Console_Init(tUART * UART)
{
    UINT status;
    
    /* Initialize console data */
    console->UART_Handler = UART;
    console->RX_Buff_Idx = 0;
    console->Console_State = eConsole_Wait_For_Commands;
    console->Complete_Task = Null_Task;
    console->Complete_Params = NULL;
    console->Complete_Need_Update = false;
    
    /* Create ThreadX synchronization objects */
    status = tx_mutex_create(&console_mutex, "CONSOLE_MUTEX", TX_INHERIT);
    if (status != TX_SUCCESS) {
        printd("ERROR: Console mutex creation failed: %u\r\n", status);
        return;
    }
    
    status = tx_event_flags_create(&console_events, "CONSOLE_EVENTS");
    if (status != TX_SUCCESS) {
        printd("ERROR: Console events creation failed: %u\r\n", status);
        return;
    }
    
    /* Initialize queues */
    console->Console_Commands = Prep_Queue();
    console->Running_Repeat_Commands = Prep_Queue();
    
    if (!console->Console_Commands || !console->Running_Repeat_Commands) {
        printd("ERROR: Queue initialization failed\r\n");
        return;
    }
    
    /* Create threads with proper priorities */
    status = tx_thread_create(&rx_thread, "CONSOLE_RX", RX_Thread_Entry, 0,
                             rx_thread_stack, TX_APP_THREAD_STACK_SIZE,
                             3, 3, TX_NO_TIME_SLICE, TX_AUTO_START);
    if (status != TX_SUCCESS) {
        printd("ERROR: RX thread creation failed: %u\r\n", status);
        return;
    }
    
    status = tx_thread_create(&debug_thread, "CONSOLE_DEBUG", Debug_Thread_Entry, 0,
                             debug_thread_stack, TX_SMALL_APP_THREAD_STACK_SIZE,
                             5, 5, TX_NO_TIME_SLICE, TX_AUTO_START);
    if (status != TX_SUCCESS) {
        printd("ERROR: Debug thread creation failed: %u\r\n", status);
        return;
    }
    
    status = tx_thread_create(&complete_thread, "CONSOLE_CMD", Complete_Thread_Entry, 0,
                             complete_thread_stack, TX_SMALL_APP_THREAD_STACK_SIZE,
                             4, 4, TX_NO_TIME_SLICE, TX_AUTO_START);
    if (status != TX_SUCCESS) {
        printd("ERROR: Complete thread creation failed: %u\r\n", status);
        return;
    }
    
    /* Add default commands */
    Console_Add_Command("clear", "Clear the screen", Clear_Screen, NULL);
    
    printd("\r\nThreadX Console Initialized\r\nInput Command: \r\n");
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
    
    /* Free all commands and their allocations */
    if (console->Console_Commands) {
        /* Free each command's strings and structures manually */
        TX_MUTEX *queue_mutex = Queue_Get_Mutex(console->Console_Commands);
        if (queue_mutex && tx_mutex_get(queue_mutex, TX_WAIT_FOREVER) == TX_SUCCESS) {
            for (int i = 0; i < console->Console_Commands->Size; i++) {
                tConsole_Command *cmd = (tConsole_Command *)Queue_Peek(console->Console_Commands, i);
                if (cmd) {
                    if (cmd->Command_Name) Safe_Block_Release(cmd->Command_Name);
                    if (cmd->Description) Safe_Block_Release(cmd->Description);
                    Safe_Block_Release(cmd);
                }
            }
            tx_mutex_put(queue_mutex);
        }
        
        /* Empty the queue without calling Free_Queue (which would try to free with wrong allocator) */
        while (Dequeue(console->Console_Commands) != NULL) {
            /* Commands already freed above, just drain the queue */
        }
        tx_mutex_delete(Queue_Get_Mutex(console->Console_Commands));
        Safe_Block_Release(console->Console_Commands);
        console->Console_Commands = NULL;
    }
    
    if (console->Running_Repeat_Commands) {
        /* Running commands are references to commands in Console_Commands, 
           so just drain and delete the queue structure */
        while (Dequeue(console->Running_Repeat_Commands) != NULL) {
            /* Just drain - don't free the command pointers */
        }
        tx_mutex_delete(Queue_Get_Mutex(console->Running_Repeat_Commands));
        Safe_Block_Release(console->Running_Repeat_Commands);
        console->Running_Repeat_Commands = NULL;
    }
    
    /* Delete synchronization objects */
    tx_mutex_delete(&console_mutex);
    tx_event_flags_delete(&console_events);
}

tConsole_Command * Console_Add_Command(const char * command_Name, const char * Description,
                                            void (*Call_Function)(void *), void * Call_Params)
{
    tConsole_Command * new_Command = NULL;
    UINT status;
    
    /* Allocate command structure */
    status = Safe_Block_Allocate(&tx_app_mid_block_pool, (VOID **)&new_Command, TX_NO_WAIT);
    if (status != TX_SUCCESS) {
        printd("ERROR: Command allocation failed\r\n");
        return NULL;
    }
    
    /* Allocate and copy command name */
    status = Safe_Block_Allocate(&tx_app_mid_block_pool, (VOID **)&new_Command->Command_Name, TX_NO_WAIT);
    if (status != TX_SUCCESS) {
        Safe_Block_Release(new_Command);
        printd("ERROR: Command name allocation failed\r\n");
        return NULL;
    }
    strcpy(new_Command->Command_Name, command_Name);
    
    /* Allocate and copy description if provided */
    if (Description != NULL) {
        status = Safe_Block_Allocate(&tx_app_large_block_pool, (VOID **)&new_Command->Description, TX_NO_WAIT);
        if (status != TX_SUCCESS) {
            Safe_Block_Release(new_Command->Command_Name);
            Safe_Block_Release(new_Command);
            printd("ERROR: Description allocation failed\r\n");
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
    new_Command->Last_Run_Tick = 0;
    
    /* Add to queue */
    if (!Enqueue(console->Console_Commands, new_Command)) {
        if (new_Command->Description) Safe_Block_Release(new_Command->Description);
        Safe_Block_Release(new_Command->Command_Name);
        Safe_Block_Release(new_Command);
        printd("ERROR: Failed to add command to queue\r\n");
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
                                                   void *Stop_Params,
                                                uint32_t repeat_time)
{
    tConsole_Command *new_Command = NULL;
    UINT status;
    
    /* Allocate command structure */
    status = Safe_Block_Allocate(&tx_app_mid_block_pool, (VOID **)&new_Command, TX_NO_WAIT);
    if (status != TX_SUCCESS) {
        printd("ERROR: Debug command allocation failed\r\n");
        return NULL;
    }
    
    /* Allocate and copy command name */
    status = Safe_Block_Allocate(&tx_app_mid_block_pool, (VOID **)&new_Command->Command_Name, TX_NO_WAIT);
    if (status != TX_SUCCESS) {
        Safe_Block_Release(new_Command);
        printd("ERROR: Debug command name allocation failed\r\n");
        return NULL;
    }
    strcpy(new_Command->Command_Name, command_Name);
    
    /* Allocate and copy description if provided */
    if (Description) {
        status = Safe_Block_Allocate(&tx_app_large_block_pool, (VOID **)&new_Command->Description, TX_NO_WAIT);
        if (status != TX_SUCCESS) {
            Safe_Block_Release(new_Command->Command_Name);
            Safe_Block_Release(new_Command);
            printd("ERROR: Debug description allocation failed\r\n");
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
    new_Command->Repeat_Time = repeat_time;  // in milliseconds
    new_Command->Last_Run_Tick = 0;
    
    /* Add to queue */
    if (!Enqueue(console->Console_Commands, new_Command)) {
        if (new_Command->Description) Safe_Block_Release(new_Command->Description);
        Safe_Block_Release(new_Command->Command_Name);
        Safe_Block_Release(new_Command);
        printd("ERROR: Failed to add debug command to queue\r\n");
        return NULL;
    }
    
    return new_Command;
}

void printd(const char* format, ...)
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
            printf("ERROR: Print buffer too large (%d bytes)\r\n", needed_size);
            return;
        }
        
        if (status != TX_SUCCESS || buffer == NULL) {
            printf("ERROR: Print buffer allocation failed\r\n");
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


// need to make the following: UART feeding is critical;
VOID RX_Thread_Entry(ULONG thread_input)
{
    (void)thread_input;
    UINT status;
    
    while (1) {
        static bool just_saw_cr = false;

        /* Receive data from UART */
        UART_Receive(console->UART_Handler, data, &data_size);
        
        if (data_size > 0) {
            /* Process received data */
            for (uint16_t counter = 0; counter < data_size; counter++) {
                /* Get current state safely */
                status = tx_mutex_get(&console_mutex, CONSOLE_MUTEX_WAIT);
                if (status != TX_SUCCESS) {
                    printd("ERROR: Failed to acquire console mutex in RX\r\n");
                    break;
                }
                tx_mutex_put(&console_mutex);
                
                /* Handle different console states */
                if (console->Console_State == eConsole_Halting_Commands) {
                    /* Stop all running commands - guard iteration with queue mutex */
                    TX_MUTEX *queue_mutex = Queue_Get_Mutex(console->Running_Repeat_Commands);
                    if (queue_mutex && tx_mutex_get(queue_mutex, TX_WAIT_FOREVER) == TX_SUCCESS) {
                        for (int i = 0; i < console->Running_Repeat_Commands->Size; i++) {
                            tConsole_Command * curr_Command = (tConsole_Command*)Queue_Peek(console->Running_Repeat_Commands, i);
                            if (curr_Command && curr_Command->Stop_Function) {
                                curr_Command->Stop_Function(curr_Command->Stop_Params);
                            }
                        }
                        tx_mutex_put(queue_mutex);
                    }
                    /* Update state safely */
                    status = tx_mutex_get(&console_mutex, CONSOLE_MUTEX_WAIT);
                    if (status == TX_SUCCESS) {
                        console->Console_State = eConsole_Halted_Commands;
                        tx_mutex_put(&console_mutex);
                    }
                }
                /* Handle resume command when halted */
                else if (console->Console_State == eConsole_Halted_Commands) {
                    if (counter >= 2 && data[counter - 2] == '!' && data[counter - 1] == 'r' && data[counter] == '\r') {
                        /* Set state directly to avoid deadlock */
                        status = tx_mutex_get(&console_mutex, CONSOLE_MUTEX_WAIT);
                        if (status == TX_SUCCESS) {
                            console->Console_State = eConsole_Resume_Commands;
                            tx_mutex_put(&console_mutex);
                        }
                    }
                }
                else if (console->Console_State == eConsole_Resume_Commands){
                    /* Resume all running commands - guard iteration with queue mutex */
                    TX_MUTEX *queue_mutex = Queue_Get_Mutex(console->Running_Repeat_Commands);
                    if (queue_mutex && tx_mutex_get(queue_mutex, TX_WAIT_FOREVER) == TX_SUCCESS) {
                        for (int i = 0; i < console->Running_Repeat_Commands->Size; i++){
                            tConsole_Command * curr_Command = (tConsole_Command*)Queue_Peek(console->Running_Repeat_Commands, i);
                            if (curr_Command && curr_Command->Resume_Function) {
                                curr_Command->Resume_Function(curr_Command->Resume_Params);
                            }
                        }
                        tx_mutex_put(queue_mutex);
                    }
                    /* Update state safely */
                    status = tx_mutex_get(&console_mutex, CONSOLE_MUTEX_WAIT);
                    if (status == TX_SUCCESS) {
                        console->Console_State = eConsole_Servicing_Command;
                        tx_mutex_put(&console_mutex);
                    }
                }
                else if (console->Console_State == eConsole_Servicing_Command) {
                    if (data[counter] == '\r') {
                        printd("Console paused.\r\n");
                        /* Set state directly to avoid deadlock */
                        status = tx_mutex_get(&console_mutex, CONSOLE_MUTEX_WAIT);
                        if (status == TX_SUCCESS) {
                            console->Console_State = eConsole_Halting_Commands;
                            tx_mutex_put(&console_mutex);
                        }
                    }
                }
                /* Handle normal command input */
                else if (console->Console_State == eConsole_Wait_For_Commands || console->Console_State == eConsole_Halted_Commands) {
                    /* Protect buffer operations */
                    status = tx_mutex_get(&console_mutex, CONSOLE_MUTEX_WAIT);
                    if (status != TX_SUCCESS) {
                        break;
                    }
                    
                    /* Check buffer overflow */
                    if (console->RX_Buff_Idx >= MAX_CONSOLE_BUFF_SIZE - 1) {
                        console->RX_Buff_Idx = 0;
                        RX_Buff_MAX_SURPASSED = true;
                    }
                    
                    /* Handle backspace */
                    if (data[counter] == '\b' || data[counter] == 0x7F) {
                        if (console->RX_Buff_Idx > 0) {
                            printd("\b \b");
                            console->RX_Buff_Idx--;
                        }
                    }

                    if (data[counter] == '\n'){
                        if (just_saw_cr){
                            just_saw_cr = false;     // swallow the LF of a CRLF pair
                            console->RX_Buff_Idx = 0;
                        }
                    }
                    
                    /* Handle normal characters */
                    else if (RX_Buff_MAX_SURPASSED == false) {
                        console->RX_Buff[console->RX_Buff_Idx] = data[counter];
                        console->RX_Buff_Idx++;
                        printd("%c", data[counter]);
                    }
                

                    /* Handle enter key */
                    if (data[counter] == '\r') {

                        if (console->RX_Buff_Idx > 0) {
                            console->RX_Buff[console->RX_Buff_Idx - 1] = '\0'; // overwrites stored '\r'
                        } else {
                            console->RX_Buff[0] = '\0'; // empty/overflow case
                        }

                        printd("\r\n");
                        
                        if (RX_Buff_MAX_SURPASSED) {
                            printd("**COMMAND TOO LONG**\r\n");
                        } else {
                            /* Copy command buffer for processing */
                            uint8_t command_buffer[MAX_CONSOLE_BUFF_SIZE];
                            uint16_t command_length = console->RX_Buff_Idx;
                            strcpy((char*)command_buffer, (char*)console->RX_Buff);
                            
                            /* Release mutex before calling Process_Commands */
                            tx_mutex_put(&console_mutex);
                            Process_Commands(command_buffer, command_length);
                            
                            /* Reacquire mutex to reset buffer state */
                            status = tx_mutex_get(&console_mutex, CONSOLE_MUTEX_WAIT);
                            if (status != TX_SUCCESS) {
                                break;
                            }
                        }
                        
                        RX_Buff_MAX_SURPASSED = false;
                        console->RX_Buff_Idx = 0;
                    }
                    just_saw_cr = (data[counter] == '\r');
                    tx_mutex_put(&console_mutex);
                }
                /* Handle quit state */
                else if (console->Console_State == eConsole_Quit_Commands) {
                    /* Stop all running commands - guard iteration with queue mutex */
                    TX_MUTEX *queue_mutex = Queue_Get_Mutex(console->Running_Repeat_Commands);
                    if (queue_mutex && tx_mutex_get(queue_mutex, TX_WAIT_FOREVER) == TX_SUCCESS) {
                        for (int i = 0; i < console->Running_Repeat_Commands->Size; i++) {
                            tConsole_Command * curr_command = Queue_Peek(console->Running_Repeat_Commands, i);
                            if (curr_command && curr_command->Stop_Function) {
                                curr_command->Stop_Function(curr_command->Stop_Params);
                            }
                        }
                        tx_mutex_put(queue_mutex);
                    }
                    /* Update state safely */
                    status = tx_mutex_get(&console_mutex, CONSOLE_MUTEX_WAIT);
                    if (status == TX_SUCCESS) {
                        console->Console_State = eConsole_Wait_For_Commands;
                        tx_mutex_put(&console_mutex);
                    }
                }
            }
            
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
        /* Execute all running debug commands - guard entire iteration with queue mutex */
        ULONG now = tx_time_get();
        TX_MUTEX *queue_mutex = Queue_Get_Mutex(console->Running_Repeat_Commands);
        if (queue_mutex && tx_mutex_get(queue_mutex, TX_WAIT_FOREVER) == TX_SUCCESS) {
            for (int i = 0; i < console->Running_Repeat_Commands->Size; i++) {
                tConsole_Command * curr_Command = (tConsole_Command *)Queue_Peek(console->Running_Repeat_Commands, i);
                if (!curr_Command || !curr_Command->Call_Function) continue;

                /* If Repeat_Time is zero, run every cycle. Otherwise check elapsed time. */
                if (curr_Command->Repeat_Time == 0) {
                    curr_Command->Call_Function(curr_Command->Call_Params);
                    curr_Command->Last_Run_Tick = now;
                    continue;
                }

                /* Convert Repeat_Time (ms) to ticks. Round up to avoid running too frequently. */
                UINT ticks_needed = (UINT)((((uint64_t)curr_Command->Repeat_Time) * TX_TIMER_TICKS_PER_SECOND + 999) / 1000);

                /* Calculate elapsed ticks handling wrap-around by unsigned subtraction. */
                ULONG elapsed = now - curr_Command->Last_Run_Tick;

                if (curr_Command->Last_Run_Tick == 0 || elapsed >= ticks_needed) {
                    curr_Command->Call_Function(curr_Command->Call_Params);
                    curr_Command->Last_Run_Tick = now;
                }
            }
            tx_mutex_put(queue_mutex);
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
            if (console->Complete_Need_Update && console->Complete_Task != NULL && console->Complete_Task != Null_Task) {
                /* Run the task once */
                console->Complete_Task(console->Complete_Params);
                /* Reset flag, task, and params */
                console->Complete_Need_Update = false;
                console->Complete_Task = Null_Task;
                console->Complete_Params = NULL;
            }
            tx_mutex_put(&console_mutex);
        }
        
        tx_thread_sleep(CONSOLE_THREAD_SLEEP_MS);
    }
}

static void Process_Commands(uint8_t * data_ptr, uint16_t command_size)
{
    char command[MAX_CONSOLE_BUFF_SIZE];
    (void)command_size; /* Unused parameter - using fixed buffer instead */
    
    /* Ensure NUL-terminated copy with bounds checking */
    command[0] = '\0';
    snprintf(command, sizeof(command), "%s", (char*)data_ptr);
    
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
        printd("\r\n");
        /* Guard iteration with queue mutex */
        TX_MUTEX *queue_mutex = Queue_Get_Mutex(console->Console_Commands);
        if (queue_mutex && tx_mutex_get(queue_mutex, TX_WAIT_FOREVER) == TX_SUCCESS) {
            for (int i = 0; i < console->Console_Commands->Size; i++) {
                tConsole_Command * curr_Command = (tConsole_Command *)Queue_Peek(console->Console_Commands, i);
                if (curr_Command) {
                    printd("%s: %s\r\n", curr_Command->Command_Name, 
                                curr_Command->Description ? curr_Command->Description : "No description");
                }
            }
            tx_mutex_put(queue_mutex);
        }
    }
    /* Handle quit command */
    else if (strcmp(command, "quit") == 0) {
        printd("Quitting commands.\r\n");
        Console_Quit_Commands();
    }
    /* Handle prefixed commands (halt/stop/help <command>) */
    else if (flag_3 && strlen(command) > 5) {
        char prompt_command[MAX_CONSOLE_BUFF_SIZE];
        /* Safely copy command after prefix, with bounds checking */
        snprintf(prompt_command, sizeof(prompt_command), "%s", command + 5);
        
        /* Guard iteration with queue mutex */
        TX_MUTEX *queue_mutex = Queue_Get_Mutex(console->Console_Commands);
        if (queue_mutex && tx_mutex_get(queue_mutex, TX_WAIT_FOREVER) == TX_SUCCESS) {
            for (int i = 0; i < console->Console_Commands->Size; i++) {
                tConsole_Command * curr_Command = (tConsole_Command *)Queue_Peek(console->Console_Commands, i);
                if (curr_Command && strcmp(prompt_command, curr_Command->Command_Name) == 0) {
                    if (halt_flag && curr_Command->Halt_Function) {
                        curr_Command->Halt_Function(curr_Command->Halt_Params);
                    }
                    if (stop_flag && curr_Command->Stop_Function) {
                        curr_Command->Stop_Function(curr_Command->Stop_Params);
                        Console_Quit_Commands();
                    }
                    if (help_flag) {
                        printd("%s: %s\r\n", curr_Command->Command_Name, 
                                    curr_Command->Description ? curr_Command->Description : "No description");
                    }
                    break;
                }
            }
            tx_mutex_put(queue_mutex);
        }
    }
    /* Handle resume command */
    else if (resume_flag && strlen(command) > 7) {
        char prompt_command[MAX_CONSOLE_BUFF_SIZE];
        /* Safely copy command after prefix, with bounds checking */
        snprintf(prompt_command, sizeof(prompt_command), "%s", command + 7);
        
        /* Guard iteration with queue mutex */
        TX_MUTEX *queue_mutex = Queue_Get_Mutex(console->Console_Commands);
        if (queue_mutex && tx_mutex_get(queue_mutex, TX_WAIT_FOREVER) == TX_SUCCESS) {
            for (int i = 0; i < console->Console_Commands->Size; i++) {
                tConsole_Command * curr_Command = (tConsole_Command *)Queue_Peek(console->Console_Commands, i);
                if (curr_Command && strcmp(prompt_command, curr_Command->Command_Name) == 0) {
                    if (curr_Command->Resume_Function) {
                        curr_Command->Resume_Function(curr_Command->Resume_Params);
                    }
                    break;
                }
            }
            tx_mutex_put(queue_mutex);
        }
    }
    /* Handle regular commands */
    else {
        /* Guard iteration with queue mutex */
        TX_MUTEX *commands_mutex = Queue_Get_Mutex(console->Console_Commands);
        if (commands_mutex && tx_mutex_get(commands_mutex, TX_WAIT_FOREVER) == TX_SUCCESS) {
            for (int i = 0; i < console->Console_Commands->Size; i++) {
                tConsole_Command * curr_Command = (tConsole_Command *)Queue_Peek(console->Console_Commands, i);
                if (curr_Command && strcmp(command, curr_Command->Command_Name) == 0) {
                    /* Check if command is already running - need separate mutex for running commands */
                    bool command_already_running = false;
                    TX_MUTEX *running_mutex = Queue_Get_Mutex(console->Running_Repeat_Commands);
                    if (running_mutex && tx_mutex_get(running_mutex, TX_WAIT_FOREVER) == TX_SUCCESS) {
                        for (int c = 0; c < console->Running_Repeat_Commands->Size; c++) {
                            tConsole_Command * curr_Running_Command = (tConsole_Command *)Queue_Peek(console->Running_Repeat_Commands, c);
                            if (curr_Running_Command && strcmp(command, curr_Running_Command->Command_Name) == 0) {
                                printd("Command Already Running\r\n");
                                command_already_running = true;
                                break;
                            }
                        }
                        tx_mutex_put(running_mutex);
                    }
                    
                    if (!command_already_running && curr_Command->Call_Function) {
                        printd("Starting %s command.\r\n", curr_Command->Command_Name);
                        
                        /* Handle different command types */
                        if (curr_Command->Command_Type == eConsole_Debug_Command) {
                            /* Execute debug commands immediately and add to running list */
                            curr_Command->Call_Function(curr_Command->Call_Params);
                            Enqueue(console->Running_Repeat_Commands, curr_Command);
                        }
                        else if (curr_Command->Command_Type == eConsole_Full_Command) {
                            /* Set up full commands to run in complete thread */
                            UINT status = tx_mutex_get(&console_mutex, CONSOLE_MUTEX_WAIT);
                            if (status == TX_SUCCESS) {
                                console->Complete_Task = curr_Command->Call_Function;
                                console->Complete_Params = curr_Command->Call_Params;
                                console->Complete_Need_Update = true;
                                tx_mutex_put(&console_mutex);
                            }
                        }
                    }
                    break;
                }
            }
            tx_mutex_put(commands_mutex);
        }
    }
}

static void Clear_Screen(void * unused)
{
    printd("\033[2J");
    printd("%c[2j%c[H", 27, 27);
}

void Console_Pause_Commands(void)
{
    UINT status = tx_mutex_get(&console_mutex, CONSOLE_MUTEX_WAIT);
    if (status == TX_SUCCESS) {
        console->Console_State = eConsole_Halting_Commands;
        tx_mutex_put(&console_mutex);
    }
}

void Console_Quit_Commands(void)
{
    UINT status = tx_mutex_get(&console_mutex, CONSOLE_MUTEX_WAIT);
    if (status == TX_SUCCESS) {
        console->Console_State = eConsole_Quit_Commands;
        tx_mutex_put(&console_mutex);
    }
}

void Console_Resume_Commands(void)
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


