/*
 * console.c
 *
 *  Created on: feb 22, 2025
 *      Author: jason.peng
 */


// note: to initialize a string, do A[] = "the quick brown fox jumped over the lazy dog"
// note: to send data that's been recieved into a buffer to something else via DMA, no CPU is needed 
// since it's from RAM to buffer. This means to print console commands, just "printd" the entire 
// buffer - it will be copied directly via memcpy to the data buffer of the queued up... wait...
// is this slower?
// no, for larger messages. for large messages: use DMA. change the pointer of "data" to the pointer of 
// "string".

//need to do stack checking to minimize each individual thread; include a stack checking print loop, etc. that prints out over time for each stack
// can also do byte and block checking to see bytes and blocks

/**
 * to do: 1) need to make console work with complete vs debug command
 * 2) need to make console work with threadX according to recommendations
 */

#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include "console.h"


static tConsole console_data;
static tConsole * console = &console_data;

/* ThreadX thread objects and stacks */
static TX_THREAD rx_thread;
static TX_THREAD debug_thread;
static TX_THREAD complete_thread;
static UCHAR rx_thread_stack[TX_APP_THREAD_STACK_SIZE];
static UCHAR debug_thread_stack[TX_SMALL_APP_THREAD_STACK_SIZE]; //416 - stack checking to minimize each individual
static UCHAR complete_thread_stack[TX_SMALL_APP_THREAD_STACK_SIZE];

static TX_MUTEX console_mutex;



uint8_t data[UART_RX_BUFF_SIZE];
uint16_t data_size;

//
static void RX_Task(void * NULL_Ptr); // need to get rid of and make directly into thread
static void Debug_Runner_Task(void * NULL_Ptr); // need to get rid of and make directly into thread
//


static void Process_Commands(uint8_t * data_ptr, uint16_t command_size); 
static void Clear_Screen(void);
static void Pause_Commands(void);
static void Resume_Commands(void);
static void Quit_Commands(void);

static bool RX_Buff_MAX_SURPASSED = false;

//typeConsole:
/**
 * UART_Handler: UART Handle for the UART that the console is using
 * TX_Buff[MAX_CONSOLE_SIZE]; a buffer for the TX Console. 
 * TX_Buff_Idx: The index at which the consoel reads the TX buffer
 * RX_Buff[MAX_CONSOLE_SIZE]; buffer for the RX side of the console process
 * RX_Buff_Idx:     To do: figure out how RX process works?
 */

void Init_Console(tUART * UART){
    
    console->UART_Handler = UART;
    console->RX_Buff_Idx = 0;
    console->Console_State = eConsole_Wait_For_Commands;

    tx_thread_create(&rx_thread, "CONSOLE_RX", RX_Thread, 0,
                     rx_thread_stack, TX_APP_THREAD_STACK_SIZE,
                     5, 5, TX_NO_TIME_SLICE, TX_AUTO_START);
    tx_thread_create(&debug_thread, "CONSOLE_DEBUG", Debug_Thread, 0,
                     debug_thread_stack, TX_APP_THREAD_STACK_SIZE,
                     5, 5, TX_NO_TIME_SLICE, TX_AUTO_START); // execute every debug command every 200 ms
    
                     tx_mutex_create(&console_mutex, "CONSOLE_MUTEX", TX_INHERIT);   


    console->Console_Commands = Prep_Queue();
    console->Running_Repeat_Commands = Prep_Queue();
    Add_Console_Command("clear", "Clear the screen", Clear_Screen, NULL); // needs to be looked into


    printd("\r\nInput Command: \r\n");
    console->Complete_Task = Null_Task;
    tx_thread_create(&complete_thread, "CONSOLE_CMD", Complete_Thread, 0,
                     complete_thread_stack, TX_APP_THREAD_STACK_SIZE,
                     5, 5, TX_NO_TIME_SLICE, TX_AUTO_START);
    console->Complete_Need_Update = false;
}


// one console to handle all the full commands, each additional call is a new console
// new console killed after debug task ended
// one main console to handle all the tasks RX, TX, full tasks, and keep track of main queues (Console commands to execute, )
// Description should be formatted as follows: str[] = "HELLOO WORLDDDD"
tConsole_Command * Init_Reg_Command(const char * command_Name, const char * Description, void * Call_Function, void * Call_Params){
    
    tConsole_Command * new_Command = NULL;
    UINT status = tx_block_allocate(&tx_app_mid_block_pool, (VOID **)&new_Command, TX_NO_WAIT);
    if (status != TX_SUCCESS){
        return NULL;
    }

    status =  tx_block_allocate(&tx_app_mid_block_pool, (VOID **)&new_Command->Command_Name, TX_NO_WAIT);
    if (status != TX_SUCCESS){
        tx_block_release(new_Command);
        return NULL;
    }

    strcpy(new_Command->Command_Name, command_Name);

    if (Description != NULL){
        status = tx_block_allocate(&tx_app_large_block_pool, (VOID **)&new_Command->Description, TX_NO_WAIT);
        if (status != TX_SUCCESS){
            tx_block_release(new_Command);
            tx_block_release(new_Command->Command_Name);
            return NULL;
        }
        strcpy(new_Command->Description, Description);
    }

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

    bool ret = Enqueue(console->Console_Commands, new_Command);
    if (ret != true){
        tx_block_release(new_Command);
        tx_block_release(new_Command->Command_Name);
        tx_block_release(new_Command->Description);
        return NULL;
    } else {return new_Command;}

}


tConsole_Command *
Init_Debug_Command(const char *command_Name,
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
    UINT             status;

    /* 1) Allocate the command struct itself */
    status = tx_block_allocate(&tx_app_mid_block_pool,
                               (VOID **)&new_Command,
                               TX_NO_WAIT);
    if (status != TX_SUCCESS) {
        printd("DBG alloc cmd block failed\r\n");
        return NULL;
    }

    /* 2) Allocate & copy the name (mid-size pool) */
    status = tx_block_allocate(&tx_app_mid_block_pool,
                               (VOID **)&new_Command->Command_Name,
                               TX_NO_WAIT);
    if (status != TX_SUCCESS) {
        printd("DBG alloc name block failed\r\n");
        tx_block_release(new_Command);
        return NULL;
    }
    strcpy(new_Command->Command_Name, command_Name);

    /* 3) Allocate & copy the description if provided (large pool) */
    if (Description) {
        status = tx_block_allocate(&tx_app_large_block_pool,
                                   (VOID **)&new_Command->Description,
                                   TX_NO_WAIT);
        if (status != TX_SUCCESS) {
            printd("DBG alloc desc block failed\r\n");
            tx_block_release(new_Command->Command_Name);
            tx_block_release(new_Command);
            return NULL;
        }
        strcpy(new_Command->Description, Description);
    }
    else {
        new_Command->Description = NULL;
    }

    /* 4) Fill in the rest of the fields */
    new_Command->Command_Type   = eConsole_Debug_Command;
    new_Command->Call_Function  = Call_Function;
    new_Command->Call_Params    = Call_Params;

    new_Command->Halt_Function   = Halt_Function;
    new_Command->Halt_Params     = Halt_Params;
    new_Command->Resume_Function = Resume_Function;
    new_Command->Resume_Params   = Resume_Params;
    new_Command->Stop_Function   = Stop_Function;
    new_Command->Stop_Params     = Stop_Params;

    /* 5) Default repeat time for debug commands */
    new_Command->Repeat_Time = 50;  // ms

    /* 6) Enqueue and clean up on failure */
    if (!Enqueue(console->Console_Commands, new_Command)) {
        printd("DBG enqueue failed\r\n");
        if (new_Command->Description)     tx_block_release(new_Command->Description);
        tx_block_release(new_Command->Command_Name);
        tx_block_release(new_Command);
        return NULL;
    }

    return new_Command;
}

//clear screen
static void Clear_Screen(void){
	printf("\033[2J");
	printf("%c[2j%c[H",27,27);
}

//DMA print
void printd(const char* format, ...) {
    va_list args;
    const char* percent_sign = strchr(format, '%');
    if (percent_sign == NULL) {
        UART_Add_Transmit(console->UART_Handler, (uint8_t *)format, strlen(format));
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
    HAL_UART_Transmit(console->UART_Handler, (uint8_t*)&ch, 1, PRINTF_DELAY_TIME);  // Blocking call
    return ch;
}



/* RX functionality:
console has a 2 states:
1) wait for commands/halted
    hitting enter here causes commands to to be inputted
    can resume with 'r', pause any commands with !p Command or stop any commands with !q Command - 
    these are all processed by the process commands function
2) servicing
    does not take any inputs, cannot input anything into the queue, and the only thing
    it takes is enter to pause

*/


//need to map out design
 
static void RX_Task(void * NULL_Ptr){
    //malloc a buffer to recieve the DMA transmission -. can't use backspace cus then can't match to stuff like \b or \r
    data[UART_RX_BUFF_SIZE];
    //recieve the data into the buffer
    UART_Receive(console->UART_Handler, data, &data_size);
    //go thru the buffer using a counter to the data_size
    for (uint16_t counter = 0; counter < data_size; counter++){
    // if paused: check for resume command
        if (console->Console_State == eConsole_Halting_Commands){
            int i = 0;
            for (; i < console->Running_Repeat_Commands->Size; i++){
                tConsole_Command * curr_Command = (tConsole_Command*)Queue_Peek(console->Running_Repeat_Commands, i);
                curr_Command->Stop_Function(curr_Command->Stop_Params);
            }
            console->Console_State = eConsole_Halted_Commands;
        }
        // no longer check if halted, but rather, resume every guy who isn't paused
        if (console->Console_State == eConsole_Halted_Commands){ 
            if (data[counter - 2] == '!' && data[counter - 1] == 'r' && data[counter] == '\r'){
                Resume_Commands();
            }
        }
        if (console->Console_State == eConsole_Resume_Commands){// easy, just resume all same as stopped} 
            int i = 0;
            for (; i < console->Running_Repeat_Commands->Size; i++){
                tConsole_Command * curr_Command = (tConsole_Command*)Queue_Peek(console->Running_Repeat_Commands, i);
                curr_Command->Resume_Function(curr_Command->Resume_Params);
            }
            console->Console_State = eConsole_Servicing_Command;
        }

    // otherwise, possibly paused OR waiting;
    // 3 cases: either backspace, enter, or another character
        if (console->Console_State == eConsole_Wait_For_Commands || console->Console_State == eConsole_Halted_Commands) {
        //if backspace: transmit a backspace and decrement the index of the UART buffer (since want to overwrite, go backwards one)
            if (console->RX_Buff_Idx >= MAX_CONSOLE_BUFF_SIZE-1){
                console->RX_Buff_Idx = 0;
                RX_Buff_MAX_SURPASSED = true;
            } 
            if (data[counter] == '\b' || data[counter] == '0x7F'){
                if (console->RX_Buff_Idx > 0){
                    printd("\b \b");
                    console->RX_Buff_Idx--;
                }
            }
     
        //if we get a return, null terminate the buff, print a new line, then store the command and process_command(); then clear the command buffer; if command is too long, (clear buffer)
            else if (RX_Buff_MAX_SURPASSED == false) {
                console->RX_Buff[console->RX_Buff_Idx] = data[counter];
                console->RX_Buff_Idx++;
                printd((const char *)data[counter);
            }
            if (data[counter] == '\r'){
            console->RX_Buff[console->RX_Buff_Idx - 1] = '\0';
            
                printf("\r\n");
                if (RX_Buff_MAX_SURPASSED) {
                    printd("\r\n**COMMAND TOO LONG**\r\n");
                } else {
                    Process_Command(console->RX_Buff, console->RX_Buff_Idx);
                    // in processing: if quit, then change to waiting

                    // need to change to servicing command in process() if (r) is the command
                }  
                RX_Buff_MAX_SURPASSED = false; 
            }
        } 
        else if (console->Console_State == eConsole_Servicing_Command){
            // if servicing command and enter, then halt command and move to waiting command
            if (data[counter] == '\r'){
                printf("Console paused.\r\n");
                Pause_Commands();
                }

        } else if (console->Console_State = eConsole_Quit_Commands){
            int i = 0;
            for (;i < console->Running_Repeat_Commands->Size; i++){
                tConsole_Command * curr_command;
                //note: Queue_Peek returns PTR to command
                //command itself has ptr to the actual quit function
                curr_command = Queue_Peek(console->Running_Repeat_Commands, i);
                curr_command->Stop_Function(curr_command->Stop_Params);              
                }
            console->Console_State = eConsole_Wait_For_Commands;
            }
        }
         
        memset(data, 0, UART_RX_BUFF_SIZE);
        data_size = 0;
        console->RX_Buff_Idx = 0;

    }
}

    // design: if not a debug command, dont add to running_repeat
static void Process_Commands(uint8_t * data_ptr, uint16_t command_size){
    char command[command_size];
    strcpy(command, (char *)data_ptr); // check: does strcpy cutoff at end of command_size?
    
    char prompt_help_stop[4]; // size of help or quit
    strncpy(prompt_help_stop, command, 4);
    prompt_help_stop[4] = '\0';

    bool help_flag = false;
    bool stop_flag = false;
    bool flag_3 = false; // any of help, halt, stop

    if (strcmp(prompt_help_stop, "stop") == 0){
        stop_flag = true;
        flag_3 = true;
    }
    if (strcmp(prompt_help_stop, "help") == 0){
        help_flag = true;
        flag_3 = true;
    }

    if (strcmp(command, "help") == 0){
        printd("\r\n");
        int i = 0;
        for (; i < console->Console_Commands->Size; i++){
            tConsole_Command * curr_Command = (tConsole_Command *)Queue_Peek(console->Console_Commands, i);
            printd("%s: %s\r\n", curr_Command->Command_Name, curr_Command->Description);
        }
    }
    else if (strcmp(command, "quit") == 0){
        printd("Quitting commands.\r\n");
        Quit_Commands();
    }
    else if (flag_3){
        char prompt_command[command_size - 5]; // 5 to account for the space
        strcpy(prompt_command, command + 5);
        for (int i = 0; i < console->Console_Commands->Size; i++){
        tConsole_Command * curr_Command = (tConsole_Command *)Queue_Peek(console->Console_Commands, i);
        if (strcmp(prompt_command, curr_Command->Command_Name) == 0){
            if (stop_flag){
                curr_Command->Stop_Function(curr_Command->Stop_Params);
            }
            if (help_flag) {
                printd("%s: %s\r\n", curr_Command->Command_Name, curr_Command->Description);
                }
            }
        }
    }
    else {
        for (int i = 0; i < console->Console_Commands->Size; i++){
            tConsole_Command * curr_Command = (tConsole_Command *)Queue_Peek(console->Console_Commands, i);
            if (strcmp(command, curr_Command->Command_Name) == 0){
                bool command_alrdy_running = false;
                for (int c = 0; c < console->Running_Repeat_Commands->Size; c++){
                    tConsole_Command * curr_Running_Command = (tConsole_Command *)Queue_Peek(console->Running_Repeat_Commands, c);
                    if (strcmp(command, curr_Running_Command->Command_Name)){
                        printd("Command Already Running\r\n");
                        command_alrdy_running = true;
                    }
                }
                if (!command_alrdy_running){
                    curr_Command->Call_Function(curr_Command->Call_Params);
                    printd("Starting %s command. \r\n", curr_Command->Command_Name);
                }
            }
        }
    }
}
   
    // if a debug command, then add to running_repeat
static void Debug_Runner_Task(void * NULL_Ptr){
    for (int i = 0; i < console->Running_Repeat_Commands; i++){
        tConsole_Command * curr_Command = (tConsole_Command *)Queue_Peek(console->Running_Repeat_Commands, i);
        curr_Command->Call_Function(curr_Command->Call_Params);
    }
}

void Pause_Commands(void){
    console->Console_State = eConsole_Halting_Commands;
    return;
}

void Quit_Commands(void){
    console->Console_State = eConsole_Quit_Commands;
    return;
}

void Resume_Commands(void){
    console->Console_State = eConsole_Resume_Commands;
    return;
}


/* ThreadX thread entry functions */
static VOID RX_Thread_Entry(ULONG thread_input)
{
    (void)thread_input;
    while (1)
    {
        RX_Task(NULL);
        tx_thread_sleep(1);
    }
}

static VOID Debug_Thread_Entry(ULONG thread_input)
{
    (void)thread_input;
    while (1)
    {
        Debug_Runner_Task(NULL);
        tx_thread_sleep(200);
    }
}


//need to change this: runs the complete task, and if it's been run, then change it to NULL
static VOID Complete_Thread_Entry(ULONG thread_input)
{
    (void)thread_input;
    while (1)
    {
        if (console->Complete_Task != NULL)
        {
            console->Complete_Task(NULL);
        }
        tx_thread_sleep(1);
    }
}

static void Null_Task(void * NULL_Ptr)
{
    (void)NULL_Ptr;
    return;
}


