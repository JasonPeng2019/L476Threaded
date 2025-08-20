/*
 * SPI.c
 *
 *  Created on: Apr 3, 2020
 *      Author: devink
 */

#include "SPI.h"
#include "Scheduler/Scheduler.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static bool SPI_Callbacks_Initialized = false;
static Queue SPI_Callback_Handles;
static void SPI_Tasks(void * Task_Data);

SPI * Init_SPI(SPI_HandleTypeDef * SPI_Handle)
{
	if(!SPI_Callbacks_Initialized)
	{
		Prep_Queue(&SPI_Callback_Handles);
		SPI_Callbacks_Initialized = true;
	}

	SPI * spi = (SPI *)malloc(sizeof(SPI));
	if(spi)
	{
		spi->SPI_Handle = SPI_Handle;
		spi->SPI_Busy = false;
		spi->Current_Task = NULL;

		Prep_Queue(&spi->Task_Queue);

		spi->Task_ID = Start_Task(SPI_Tasks, (void *)spi, 0);
		Set_Task_Name(spi->Task_ID, "SPI Task");
		Task_Add_Heap_Size(spi->Task_ID, (void *) spi);

		// Add the spi to the callback queue so we can handle HAl callbacks
		Enqueue(&SPI_Callback_Handles, (void *)spi);
	}
	else
	{
		printf("SPI malloc error\r\n");
	}

	return spi;
}

static void SPI_Tasks(void * Task_Data)
{
	SPI * spi = (SPI *)Task_Data;

	// If the spi is not busy and there is something to do then process the next task
	if(!spi->SPI_Busy && spi->Task_Queue.Size > 0)
	{
		// Set the flag that we are busy
		spi->SPI_Busy = true;

		// Test if we need to clean up previous data
		if(spi->Current_Task != NULL)
		{
			Task_free(spi->Task_ID, spi->Current_Task->Transmit_Data);
			Task_free(spi->Task_ID, spi->Current_Task);
		}

		// Get the next task to process
		spi->Current_Task = (SPI_Task *)Dequeue(&spi->Task_Queue);

		// Call the pre function if there is one defined
		if(spi->Current_Task->Pre_Function != NULL)
		{
			spi->Current_Task->Pre_Function(spi->Current_Task->Function_Data);
		}

		// Set the chip select low
		Set_GPIO_State_Low(spi->Current_Task->nSS);

		// Start the transmission
		HAL_SPI_Transmit_DMA(spi->SPI_Handle, spi->Current_Task->Transmit_Data, spi->Current_Task->Transmit_Data_Size);
	}
}

int32_t SPI_Write(SPI * SPI_Handle, GPIO * nSS, uint8_t * Transmit_Data, uint32_t Transmit_Data_Size)
{
	uint32_t Data_Transmitted = 0;
	HAL_StatusTypeDef ret;
	Set_GPIO_State_Low(nSS);
	while(Data_Transmitted < Transmit_Data_Size)
	{
		uint16_t size = 0;
		if((Transmit_Data_Size - Data_Transmitted) > 0xFFFF)
			size = 0xFFFF;
		else
			size = Transmit_Data_Size - Data_Transmitted;

		ret = HAL_SPI_Transmit(SPI_Handle->SPI_Handle, &Transmit_Data[Data_Transmitted], size, MAX_SPI_WAIT_TIME);
		Data_Transmitted += size;
	}
	Set_GPIO_State_High(nSS);

	if(ret == HAL_OK)
		return Transmit_Data_Size;
	else if(ret == HAL_ERROR)
		return eSPI_Failed;
	else if(ret == HAL_BUSY)
		return eSPI_Busy;
	else if(ret == HAL_TIMEOUT)
		return eSPI_Timeout;
	else
		return 0;
}

int32_t SPI_Read(SPI * SPI_Handle, GPIO * nSS, uint8_t * Return_Data, uint16_t Return_Data_Size)
{
	Set_GPIO_State_Low(nSS);
	HAL_StatusTypeDef ret = HAL_SPI_Receive(SPI_Handle->SPI_Handle, Return_Data, Return_Data_Size, MAX_SPI_WAIT_TIME);
	Set_GPIO_State_High(nSS);

	if(ret == HAL_OK)
		return Return_Data_Size;
	else if(ret == HAL_ERROR)
		return eSPI_Failed;
	else if(ret == HAL_BUSY)
		return eSPI_Busy;
	else if(ret == HAL_TIMEOUT)
		return eSPI_Timeout;
	else
		return 0;
}

int32_t SPI_Addressed_Write(SPI * SPI_Handle, GPIO * nSS, uint8_t * Address_Data, uint16_t Address_Data_Size, uint8_t * Transmit_Data, uint16_t Transmit_Data_Size)
{
	Set_GPIO_State_Low(nSS);
	HAL_StatusTypeDef ret = HAL_SPI_Transmit(SPI_Handle->SPI_Handle, Address_Data, Address_Data_Size, MAX_SPI_WAIT_TIME);
	if(ret == HAL_OK)
		ret = HAL_SPI_Transmit(SPI_Handle->SPI_Handle, Transmit_Data, Transmit_Data_Size, MAX_SPI_WAIT_TIME);
	Set_GPIO_State_High(nSS);
	
	if(ret == HAL_OK)
		return Transmit_Data_Size;
	else if(ret == HAL_ERROR)
		return eSPI_Failed;
	else if(ret == HAL_BUSY)
		return eSPI_Busy;
	else if(ret == HAL_TIMEOUT)
		return eSPI_Timeout;
	else
		return 0;
}

int32_t SPI_Addressed_Read(SPI * SPI_Handle, GPIO * nSS, uint8_t * Transmit_Data, uint16_t Transmit_Data_Size, uint8_t * Return_Data, uint16_t Return_Data_Size)
{
	Set_GPIO_State_Low(nSS);
	HAL_StatusTypeDef ret = HAL_SPI_Transmit(SPI_Handle->SPI_Handle, Transmit_Data, Transmit_Data_Size, MAX_SPI_WAIT_TIME);
	if(ret == HAL_OK)
		ret = HAL_SPI_Receive(SPI_Handle->SPI_Handle, Return_Data, Return_Data_Size, MAX_SPI_WAIT_TIME);
	Set_GPIO_State_High(nSS);

	if(ret == HAL_OK)
		return Return_Data_Size;
	else if(ret == HAL_ERROR)
		return eSPI_Failed;
	else if(ret == HAL_BUSY)
		return eSPI_Busy;
	else if(ret == HAL_TIMEOUT)
		return eSPI_Timeout;
	else
		return 0;
}


int32_t SPI_Write_DMA(SPI * SPI_Handle, GPIO * nSS, uint8_t * Transmit_Data, uint16_t Transmit_Data_Size, void * Pre_Function_PTR, void * Post_Function_PTR, void * Function_Data)
{
	// Save all the data and queue to be processed when the bus is free
	SPI_Task * task = (SPI_Task *)Task_malloc(SPI_Handle->Task_ID, sizeof(SPI_Task));
	if(task != NULL)
	{
		task->Transmit_Data = (uint8_t *)Task_malloc(SPI_Handle->Task_ID, Transmit_Data_Size * sizeof(uint8_t));
		if(task->Transmit_Data != NULL)
		{
			task->Function_Data = Function_Data;
			task->Post_Function = Post_Function_PTR;
			task->Pre_Function = Pre_Function_PTR;
			memcpy(task->Transmit_Data, Transmit_Data, Transmit_Data_Size);
			task->Transmit_Data_Size = Transmit_Data_Size;
			task->Type = eWrite_DMA;
			task->nSS = nSS;

			Enqueue(&SPI_Handle->Task_Queue, (void *)task);

			return Transmit_Data_Size;
		}
		else
			Task_free(SPI_Handle->Task_ID, task);
	}

	return eSPI_Failed;
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
	// Search for the correct spi handle
	for(int c = 0; c < SPI_Callback_Handles.Size; c++)
	{
		SPI * spi = (SPI *)Queue_Peek(&SPI_Callback_Handles, c);
		if(spi->SPI_Handle == hspi)
		{
			// We have found the spi handle the callback is for

			// Set the chip select high
			Set_GPIO_State_High(spi->Current_Task->nSS);

			// The transmission is complete, call the post function pointer then set the bus to free
			if(spi->Current_Task->Post_Function != NULL)
			{
				spi->Current_Task->Post_Function(spi->Current_Task->Function_Data);
			}

			spi->SPI_Busy = false;
		}
	}
}
