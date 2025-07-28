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

#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include "console.h"


static tConsole console_data;
static tConsole * console = &console_data;

/* ThreadX thread objects and stacks */
#define CONSOLE_THREAD_STACK_SIZE 512
static TX_THREAD rx_thread;
static TX_THREAD debug_thread;
static TX_THREAD complete_thread;
static UCHAR rx_thread_stack[CONSOLE_THREAD_STACK_SIZE];
static UCHAR debug_thread_stack[CONSOLE_THREAD_STACK_SIZE];
static UCHAR complete_thread_stack[CONSOLE_THREAD_STACK_SIZE];


uint8_t data[UART_RX_BUFF_SIZE];
uint16_t data_size;
static void RX_Task(void * NULL_Ptr);
static void Debug_Runner_Task(void * NULL_Ptr);
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

    tx_thread_create(&rx_thread, "CONSOLE_RX", RX_Thread_Entry, 0,
                     rx_thread_stack, TX_APP_THREAD_STACK_SIZE,
                     5, 5, TX_NO_TIME_SLICE, TX_AUTO_START);
    tx_thread_create(&debug_thread, "CONSOLE_DEBUG", Debug_Thread_Entry, 0,
                     debug_thread_stack, TX_APP_THREAD_STACK_SIZE,
                     5, 5, TX_NO_TIME_SLICE, TX_AUTO_START); // execute every debug command every 200 ms
    console->Console_Commands = Prep_Queue();
    console->Running_Repeat_Commands = Prep_Queue();
    Add_Console_Command("clear", "Clear the screen", Clear_Screen, NULL);
    printd("\r\nInput Command: \r\n");
    console->Complete_Task = Null_Task;
    tx_thread_create(&complete_thread, "CONSOLE_CMD", Complete_Thread_Entry, 0,
                     complete_thread_stack, TX_APP_THREAD_STACK_SIZE,
                     5, 5, TX_NO_TIME_SLICE, TX_AUTO_START);
}
// one console to handle all the full commands, each additional call is a new console
// new console killed after debug task ended
// one main console to handle all the tasks RX, TX, full tasks, and keep track of main queues (Console commands to execute, )
// Description should be formatted as follows: str[] = "HELLOO WORLDDDD"
tConsole_Command * Init_Reg_Command(const char * command_Name, const char * Description, void * Call_Function, void * Call_Params){
    tConsole_Command * new_Command = (tConsole_Command *)malloc(sizeof(tConsole_Command));
    // if malloc successful:
    if(new_Command != NULL){
        // malloc for the command->Command_Name
        new_Command->Command_Name = (const char *)malloc(sizeof(char)*(strlen(command_Name)+1));
        strcpy(new_Command->Command_Name, command_Name);
        //if malloc successful:
        if (new_Command->Command_Name != NULL){
        //malloc for the description
            new_Command->Description = (const char *)malloc(sizeof(char)*(strlen(Description)+1));
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
            Enqueue(console->Console_Commands, new_Command);
        }
    }
}


tConsole_Command * Init_Debug_Command(const char * command_Name, const char * Description,
    void * Call_Function, void * Call_Params, void * Halt_Function, void * Halt_Params, 
    void * Resume_Function, void * Resume_Params, void * Stop_Function, void * Stop_Params){
    //malloc new command
    tConsole_Command * new_Command = (tConsole_Command *)malloc(sizeof(tConsole_Command));
    // if malloc successful
    if (new_Command != NULL){
        // malloc for the command->Command Name
        new_Command->Command_Name = (const char *)malloc(sizeof(char)*strlen(command_Name));
        strcpy(new_Command->Command_Name, command_Name);
        //if malloc successful
        if (new_Command->Command_Name != NULL){
            //initialize description
            new_Command->Description = (const char *)malloc(sizeof(char)*strlen(Description));
            strcpy(new_Command->Description, Description);
            //initialize call function & init call params
            new_Command->Call_Function = Call_Function;
            new_Command->Call_Params = Call_Params;

            new_Command->Halt_Function = Halt_Function;
            new_Command->Halt_Params = Halt_Params;
            //init resume/stop function & resume/stop function params
            new_Command->Resume_Function = Resume_Function;
            new_Command->Resume_Params = Resume_Params;

            new_Command->Stop_Function = Stop_Function;
            new_Command->Stop_Params = Stop_Params;

            //assign a repeat time
            new_Command->Repeat_Time = 50; // 50 ms
            // add to commmand_queue in console
            // note: EACH new_Command is a ptr to the command
            Enqueue(console->Console_Commands, new_Command);
        }
    }
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

static void RX_Task(void * NULL_Ptr){
    //malloc a buffer to recieve the DMA transmission -. can't use backspace cus then can't match to stuff like \b or \r
    
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
        if (console->Console_State == eConsole_Halted_Commands){ 
            if (data[counter - 2] == '!' && data[counter - 1] == 'r' && data[counter] == '\r'){
                Resume_Commands();
            }
        }
        if (console->Console_State == eConsole_Resume_Commands)
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
                printd((const char *)data[counter]);
            }
            if (data[counter] == '\r'){
            console->RX_Buff[console->RX_Buff_Idx - 1] = '\0';
            console->RX_Buff_Idx = 0;
                printf("\r\n");
                if (RX_Buff_MAX_SURPASSED) {
                    printd("\r\n**COMMAND TOO LONG**\r\n");
                } else {
                    Process_Command(console->RX_Buff, console->RX_Buff_Idx);
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
            }
        }
         
        memset(data, 0, UART_RX_BUFF_SIZE);
        data_size = 0;
    }

static void Process_Commands(uint8_t * data_ptr, uint16_t command_size){
    char command[command_size];
    strcpy(command, (char *)data_ptr);
    char prompt_help_stop_halt[4]; // size of help or quit
    strncpy(prompt_help_stop_halt, command, 4);
    prompt_help_stop_halt[4] = '\0';
    char prompt_resume[6]; // size of resume
    strncpy(prompt_resume, command, 6);
    prompt_resume[6] = '\0';

    bool halt_flag = false;
    bool help_flag = false;
    bool stop_flag = false;
    bool resume_flag = false;
    bool flag_3 = false; // any of help, halt, stop
    if (strcmp(prompt_help_stop_halt, "halt") == 0){
        halt_flag = true;
        flag_3 = true;
    }
    if (strcmp(prompt_help_stop_halt, "stop") == 0){
        stop_flag = true;
        flag_3 = true;
    }
    if (strcmp(prompt_help_stop_halt, "help") == 0){
        help_flag = true;
        flag_3 = true;
    }
    if (strcmp(prompt_resume, "resume") == 0){
        resume_flag = true;
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
        char prompt_command[command_size - 4];
        strcpy(prompt_command, command + 4);
        for (int i = 0; i < console->Console_Commands->Size; i++){
        tConsole_Command * curr_Command = (tConsole_Command *)Queue_Peek(console->Console_Commands, i);
        if (strcmp(prompt_command, curr_Command->Command_Name) == 0){
            if (halt_flag){
                curr_Command->Halt_Function(curr_Command->Halt_Params);                  
            }
            if (stop_flag){
                curr_Command->Stop_Function(curr_Command->Stop_Params);
            }
            if (help_flag) {
                printd("%s: %s\r\n", curr_Command->Command_Name, curr_Command->Description);
                }
            }
        }
    }
    else if (resume_flag){
        char prompt_command[command_size - 6];
        strcpy(prompt_command, command + 6);
        for (int i = 0; i < console->Console_Commands->Size; i++){
            tConsole_Command * curr_Command = (tConsole_Command *)Queue_Peek(console->Console_Commands, i);
            if (strcmp(prompt_command, curr_Command->Command_Name == 0)){
                curr_Command->Resume_Function(curr_Command->Resume_Params);
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


