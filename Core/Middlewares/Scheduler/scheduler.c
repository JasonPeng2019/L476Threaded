/*
 * scheduler.c
 *
 *  Created on: Jun 16, 2024
 *      Author: jason.peng
 */




#include <malloc.h>
#include "scheduler.h"


static uint32_t Task_ID = 0xFFFFFFFF;

static tScheduler * Scheduler;  //keeps track of the latest ID, as well as keeps one instance of a scheduler
                                //that doesn't disappear and holds all the tasks in a queue

void Start_Scheduler(void){
	Prep_Queue(Scheduler->Tasks);
	Scheduler->Next_Task = 1;	
}

/**
 * @brief: peeks through entire scheduler tasks queue to see if it is time to execute a task.
 *   if time to execute a task, execute the task, update its runtime, last time it ran, runs count.
 * 
 * @params: none
 *
 * @return: none
 */

void Run_Scheduler_Tasks(void){
    if (Scheduler->Tasks->Size){
        tTask * task;
        // create a counter to run through the queue
        for (int i = 0; i < Scheduler->Tasks->Size; i++){
            tTask * curr_Task = (tTask*)Queue_Peek(Scheduler->Tasks, i);
        // compare current time to task start time (either when it was enqueued or last run)
            if (HAL_GetTick() - curr_Task->Last_Run_Time >= curr_Task->Wait_Time){
                if (!curr_Task->Task_Halted){
                    // if task timer has run out, execute the task
                    uint32_t start = HAL_GetTick();
                    if (curr_Task->Task_Exe != NULL){
                        curr_Task->Task_Exe(curr_Task->Task_Params);
                    }
                    uint32_t stop = HAL_GetTick();
                        //note: can do this because exe takes a void type. Thus, need to typecast params into 
                        //void when loading it to Task_Params member
                    // count time run, log it into run time
                    // increment task runs
                    curr_Task->Task_Runtime = stop - start; 
                    curr_Task->Task_Runs++; 
                    // restart task timer by updating Last_Runtime;
                }

                curr_Task->Last_Run_Time = HAL_GetTick();
            }
        }
    }
}

/**
 * @brief: initialize a task for the function (task_Function) and add it to the scheduler queue.
 *
 * @params: task execution function, execution function params, wait time
 *
 * @return: task ID of the task just added
 */
uint32_t Start_Task(void * task_Function, void * parameters, uint32_t wait_time){
//malloc the class
    tTask * new_Task = (tTask *)malloc(sizeof(tTask));
//initiate all the members of the class
    if (new_Task != NULL){    
        new_Task->Task_Halted = false;
        new_Task->Wait_Time = wait_time;
        new_Task->Task_ID = Scheduler->Next_Task;
        Scheduler->Next_Task++;
        new_Task->Task_Runs = 0;
        new_Task->Task_Runtime = 0;
        strcpy((char *)new_Task->Task_Name,"               ");
        new_Task->Heap_Use = sizeof(tTask);
        new_Task->Task_Exe = task_Function;
        new_Task->Task_Params = parameters;

        //enqeueue the task. return the failure (0) if failure (if bool, where bool is sucess/failure)
        Enqeueue(Scheduler->Tasks, new_Task);
        new_Task->Last_Run_Time = HAL_GetTick();
    } else {
        return 0;
    }
        //otherwise return task ID
    return new_Task->Task_ID;
}


/**
 * @brief: delete a task from the Task_Queue.
 * Don't update Task IDs, just in case an ID is assigned, a task is deleted
 * and then that ID needs to be referenced.
 * 
 * @params: Task ID to be deleted
 * 
 * @return: None
 */
void Delete_Task(uint32_t Task_ID){
    // void * task_Delete = Queue_Peek(Scheduler->Tasks, Task_ID); - can't do this because once a task
    //is deleted, the others wont update so your ID wont be correct
    tTask * task_Delete;
    //look through the queue for the matching task
    for (int counter = 0; counter < Scheduler->Tasks->Size; counter++){
        task_Delete = (tTask*)Queue_Peek(Scheduler->Tasks, counter);
        //if task matches, free the task params and task
        if (task_Delete->Task_ID == Task_ID){
            void * params_Delete = task_Delete->Task_Params;
            free(params_Delete);
            free(task_Delete);
            //update the scheduler's size
            Scheduler->Tasks->Size--;
            return;
        }
    }
    printf("function Delete_Task: Task not found!\r\n");
}


/**
 * @brief:changes Task_Halted to FALSE for the specific task_ID. Depends on other functions
 * to check if its halted before running task.
 * 
 * @params: Task ID to be resumed
 * 
 * @return: None
 */
void Resume_Task(uint32_t Task_ID){
    tTask * task_Resume;
    for (int counter = 0; counter < Scheduler->Tasks->Size; counter++){
        task_Resume = (tTask*)Queue_Peek(Scheduler->Tasks, counter);
        if (task_Resume->Task_ID == Task_ID){
            task_Resume->Task_Halted = false;
            return;
        }
    }
    printf("function Resume_Task: Task not found!\r\n");
}

/**
 * @brief: Task_Halted to TRUE for the specific task_ID. Depends on other functions
 * to check if its halted before running task.
 * 
 * @params: Task ID to be halted
 * 
 * @return: None
 */
void Halt_Task(uint32_t Task_ID){
    tTask * task_Halt;
    for (int counter = 0; counter < Scheduler->Tasks->Size; counter++){
        task_Halt = (tTask*)Queue_Peek(Scheduler->Tasks, counter);
        if (task_Halt->Task_ID == Task_ID){
            task_Halt->Task_Halted = true;
            return;
        }
    }
    printf("function Resume_Task: Task not found!\r\n");
}

/**
 * @brief: Changes the task wait time member between execution cycles for a specific task at Task_ID.
 * Refreshses the runtime so task wait time counter starts over
 *
 * @params: Task ID to be modified, wait time
 * 
 * @return: None
 */
void Modify_Task_Wait_Time(uint32_t Task_ID, uint32_t wait_Time){
    tTask * task_Modify;
    for (int counter = 0; counter < Scheduler->Tasks->Size; counter++){
        task_Modify = (tTask*)Queue_Peek(Scheduler->Tasks, counter);
        if (task_Modify->Task_ID == Task_ID){
            task_Modify->Wait_Time = wait_Time;
            task_Modify->Last_Run_Time = HAL_GetTick();
            return;
        }
    }
    printf("function Resume_Task: Task not found!\r\n");
}

/**
 * @brief: Changes the task name by copying the input string to the string (should by 16 empty spaces)
 * of Task->Task_Name member
 *
 * @params: Task ID to be modified, name (max 16 string)
 * 
 * @return: None
 */
void Modify_Task_Name(uint32_t Task_ID, const char * name){
    tTask * task_Modify;
    for (int counter = 0; counter < Scheduler->Tasks->Size; counter++){
        task_Modify = (tTask*)Queue_Peek(Scheduler->Tasks, counter);
        if (task_Modify->Task_ID == Task_ID){
            strncpy((char *)task_Modify->Task_Name,name,15);
            task_Modify->Task_Name[15] = '\0';
            return;
        }
    }
    printf("function Resume_Task: Task not found!\r\n");
}

tScheduler * Return_Scheduler(void){
    return &Scheduler;
}

/**
 * @brief: Malloc data of size (Size), and add that size of the data to the Task heap size member (which
 * tracks heap size). Then return a pointer to that malloc. Pointer of type void must be typecast to appropriate
 * data ptr.
 *
 * @params: Task ID of task that the object is associated with, size of object to be malloc-ed
 * 
 * @return: pointer to data, of type void
 */
void * Task_Malloc_Data(uint32_t Task_ID, uint32_t size){
//malloc the size to make a ptr
    void * data_ptr = malloc(size);
//if the malloc is successful: - always do this check when malloc-ing
    if (data_ptr != NULL){
    //then add the size to the memory tracker of the corresponding task
        tTask * tracker;
        int counter = 0;
        for (;counter < Scheduler->Tasks->Size; counter++){
            tracker = (tTask*)(Queue_Peek(Scheduler->Tasks, counter));
            if (tracker->Task_ID == Task_ID){
                tracker->Heap_Use += malloc_usable_size(data_ptr);
                return data_ptr;
            }
        }
        printf("function Task Malloc Data: Could not find task ID!\r\n");
        return data_ptr;
    }
    printf("function Task Malloc: malloc error");
    return data_ptr;
}

/**
 * @brief: free data at data_ptr and update task_heap_size accordingly by removing the size of data from 
 * heap_size member
 * 
 * @params: Task ID of task having object removed, object to be removed
 * 
 * @return: None
*/
void Task_Free(uint32_t task_ID, void * data_ptr)
{
	// Save the data size
	size_t data_size = malloc_usable_size(data_ptr);
	free(data_ptr);

	tTask * t;
	for(int counter = 0; counter < Scheduler->Tasks->Size; counter++)
	{
		t = (tTask *)Queue_Peek(Scheduler->Tasks,counter);

		if(t->Task_ID == task_ID)
		{
			t->Heap_Use -= data_size;
			return;
		}
	}
}

 /**
  * @brief: Add data size to the heap size tracker for a specific task
 *
 * @params: Task ID of task which needs data size allocation, data that size needs to be allocated for
 *
 * @return: ptr to Data that size was allocated for
 */

void Task_Add_Heap_Usage(uint32_t Task_ID, uint32_t data_Size){
    tTask * tracker;
    int counter = 0;
    for (;counter < Scheduler->Tasks->Size; counter++){
        tracker = (tTask*)(Queue_Peek(Scheduler->Tasks, counter));
        if (tracker->Task_ID == Task_ID){
            tracker->Heap_Use += data_Size;
        }
    }
}

 /**
  * @brief: Remove data size to the heap size tracker for a specific task
 *
 * @params: Task ID of task which needs data size removal, data that size needs to be removal for
 *
 * @return: None
 */
void Task_Rm_Heap_Usage(uint32_t task_ID, uint32_t data_Size)
{
	// Save the data size
	tTask * t;
	for(int counter = 0; counter < Scheduler->Tasks->Size; counter++)
	{
		t = (tTask *)Queue_Peek(Scheduler->Tasks,counter);

		if(t->Task_ID == task_ID)
		{
			t->Heap_Use -= data_Size;
			return;
		}
	}
}

void Set_Task_Name(uint32_t task, const char * name){
	tTask * t;
	uint32_t counter = 0;
	for(; counter < Scheduler->Tasks->Size; counter++)
	{
		t = (tTask *)Queue_Peek(Scheduler->Tasks,counter);

		if(t->Task_ID == task)
		{
			strncpy((char *)t->Task_Name,name,15);
			t->Task_Name[15] = '\0';

			return;
		}
	}
}

void * Null_Task(void * NULL_Ptr){
    return;
}

