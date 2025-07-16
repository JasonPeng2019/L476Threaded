/*
 * scheduler.h
 *
 *  Created on: feb 15, 2025
 *      Author: jason.peng
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include "../Queue/queue.h"


#ifndef SCHEDULER_SCHEDULER_H_
#define SCHEDULER_SCHEDULER_H_

#ifdef __cplusplus
extern "C" {
#endif


typedef struct {
	uint32_t Task_ID;
	uint32_t Wait_Time;			//wait time until task runs again
    uint32_t Last_Run_Time;
	bool Task_Halted;
	uint32_t Task_Runtime; 		// time task runs
    uint8_t Task_Runs;			// # of times task has run
	uint8_t Task_Name[16];		//
	void (*Task_Exe)(void *);
	void * Task_Params; 		//params that task_exe takes - can be whatever. Usually the handle (the only thing unique between tasks).
	uint32_t Heap_Use; 			//track how much memory has been malloc'd to task
} tTask;

typedef struct
{
	Queue * Tasks;				//queue of tasks
	uint32_t Next_Task; 		//ID of next task
}tScheduler;

#ifdef __cplusplus
}
#endif

void Run_Scheduler_Tasks(void);
uint32_t Start_Task(void * task_Function, void * parameters, uint32_t wait_time);
void Delete_Task(uint32_t Task_ID);
void Resume_Task(uint32_t Task_ID);
void Halt_Task(uint32_t Task_ID);

void Modify_Task_Wait_Time(uint32_t Task_ID, uint32_t wait_Time);
void Modify_Task_Name(uint32_t Task_ID, const char * name);

tScheduler * Return_Scheduler(void);

void * Task_Malloc_Data(uint32_t Task_ID, uint32_t size);
void Task_Free(uint32_t task_ID, void * data_ptr);
void Task_Add_Heap_Usage(uint32_t Task_ID, uint32_t data_Size);
void Task_Rm_Heap_Usage(uint32_t task_ID, uint32_t data_Size);
void Set_Task_Name(uint32_t task, const char * name);
void * Null_Task(void * NULL_Ptr);

#endif /* SCHEDULER_SCHEDULER_H_ */
