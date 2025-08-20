/*
 * cloned_SPI.c
 *
 *  Created on: Aug 19, 2025
 *      Author: Claude Code - Enhanced SPI with Circular DMA Read
 */

#include "cloned_SPI.h"
#include "Scheduler/Scheduler.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static bool Cloned_SPI_Callbacks_Initialized = false;
static Queue Cloned_SPI_Callback_Handles;
static void Cloned_SPI_Tasks(void * Task_Data);

/* Helper functions for circular buffer management */
static uint32_t Circular_Buffer_Available_Space(volatile SPI_DMA_CircularBuffer * buffer);
static uint32_t Circular_Buffer_Used_Space(volatile SPI_DMA_CircularBuffer * buffer);
static void Circular_Buffer_Update_Write_Index(volatile SPI_DMA_CircularBuffer * buffer, uint32_t bytes_written);
static uint32_t Circular_Buffer_Read_Data(volatile SPI_DMA_CircularBuffer * buffer, uint8_t * dest, uint32_t max_bytes);
static void Circular_Buffer_Init(volatile SPI_DMA_CircularBuffer * buffer);
static uint32_t Circular_Buffer_Read_From_Position(volatile SPI_DMA_CircularBuffer * buffer, uint32_t start_pos, uint8_t * dest, uint32_t bytes_to_read);

Cloned_SPI * Init_Cloned_SPI(SPI_HandleTypeDef * SPI_Handle, DMA_HandleTypeDef * DMA_RX_Handle, DMA_HandleTypeDef * DMA_TX_Handle)
{
	if(!Cloned_SPI_Callbacks_Initialized)
	{
		Prep_Queue(&Cloned_SPI_Callback_Handles);
		Cloned_SPI_Callbacks_Initialized = true;
	}

	Cloned_SPI * spi = (Cloned_SPI *)malloc(sizeof(Cloned_SPI));
	if(spi)
	{
		spi->SPI_Handle = SPI_Handle;
		spi->DMA_RX_Handle = DMA_RX_Handle;
		spi->DMA_TX_Handle = DMA_TX_Handle;
		spi->SPI_Busy = false;
		spi->Current_Task = NULL;
		spi->circular_read_active = false;

		Prep_Queue(&spi->Task_Queue);
		
		/* Initialize circular buffer */
		Circular_Buffer_Init(&spi->dma_buffer);

		spi->Task_ID = Start_Task(Cloned_SPI_Tasks, (void *)spi, 0);
		Set_Task_Name(spi->Task_ID, "Cloned SPI Task");
		Task_Add_Heap_Size(spi->Task_ID, (void *) spi);

		/* Add the spi to the callback queue so we can handle HAL callbacks */
		Enqueue(&Cloned_SPI_Callback_Handles, (void *)spi);
	}
	else
	{
		printf("Cloned SPI malloc error\r\n");
	}

	return spi;
}

void Cleanup_Cloned_SPI(Cloned_SPI * SPI_Handle)
{
	if(!SPI_Handle) return;
	
	/* Stop circular read if active */
	if(SPI_Handle->circular_read_active)
	{
		Cloned_SPI_Stop_Circular_Read(SPI_Handle);
	}
	
	/* Clean up any pending tasks */
	while(SPI_Handle->Task_Queue.Size > 0)
	{
		SPI_Task * task = (SPI_Task *)Dequeue(&SPI_Handle->Task_Queue);
		if(task)
		{
			if(task->Transmit_Data) Task_free(SPI_Handle->Task_ID, task->Transmit_Data);
			if(task->Address_Data) Task_free(SPI_Handle->Task_ID, task->Address_Data);
			Task_free(SPI_Handle->Task_ID, task);
		}
	}
	
	/* Clean up current task */
	if(SPI_Handle->Current_Task)
	{
		if(SPI_Handle->Current_Task->Transmit_Data) 
			Task_free(SPI_Handle->Task_ID, SPI_Handle->Current_Task->Transmit_Data);
		if(SPI_Handle->Current_Task->Address_Data) 
			Task_free(SPI_Handle->Task_ID, SPI_Handle->Current_Task->Address_Data);
		Task_free(SPI_Handle->Task_ID, SPI_Handle->Current_Task);
	}
	
	/* Remove from callback handles */
	for(int i = 0; i < Cloned_SPI_Callback_Handles.Size; i++)
	{
		Cloned_SPI * spi = (Cloned_SPI *)Queue_Peek(&Cloned_SPI_Callback_Handles, i);
		if(spi == SPI_Handle)
		{
			/* Remove this handle from the queue */
			Queue_Remove_At_Index(&Cloned_SPI_Callback_Handles, i);
			break;
		}
	}
	
	free(SPI_Handle);
}

static void Cloned_SPI_Tasks(void * Task_Data)
{
	Cloned_SPI * spi = (Cloned_SPI *)Task_Data;

	/* If the spi is not busy and there is something to do then process the next task */
	if(!spi->SPI_Busy && spi->Task_Queue.Size > 0)
	{
		/* Set the flag that we are busy */
		spi->SPI_Busy = true;

		/* Test if we need to clean up previous data */
		if(spi->Current_Task != NULL)
		{
			if(spi->Current_Task->Transmit_Data) 
				Task_free(spi->Task_ID, spi->Current_Task->Transmit_Data);
			if(spi->Current_Task->Address_Data) 
				Task_free(spi->Task_ID, spi->Current_Task->Address_Data);
			Task_free(spi->Task_ID, spi->Current_Task);
		}

		/* Get the next task to process */
		spi->Current_Task = (SPI_Task *)Dequeue(&spi->Task_Queue);

		/* Call the pre function if there is one defined */
		if(spi->Current_Task->Pre_Function != NULL)
		{
			spi->Current_Task->Pre_Function(spi->Current_Task->Function_Data);
		}

		/* Set the chip select low */
		Set_GPIO_State_Low(spi->Current_Task->nSS);

		/* Start the appropriate DMA operation based on task type */
		switch(spi->Current_Task->Type)
		{
			case eWrite_DMA:
				HAL_SPI_Transmit_DMA(spi->SPI_Handle, spi->Current_Task->Transmit_Data, spi->Current_Task->Transmit_Data_Size);
				break;
				
			case eAddressed_Write_DMA:
				/* First transmit the address data */
				HAL_SPI_Transmit_DMA(spi->SPI_Handle, spi->Current_Task->Address_Data, spi->Current_Task->Address_Data_Size);
				break;
				
			default:
				/* Unknown task type, mark as not busy and continue */
				spi->SPI_Busy = false;
				break;
		}
	}
}

/* BLOCKING FUNCTION CALLS - Same as original but with new structure */
int32_t Cloned_SPI_Write(Cloned_SPI * SPI_Handle, GPIO * nSS, uint8_t * Transmit_Data, uint32_t Transmit_Data_Size)
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

/**
 * potential issue when converting to threadX; need to have mutex on SPI state, otherwise, TX may start running in the middle of an RX
 * read. to avoid this, defer / block TX thread until RX is done and moved into the CRC buffer. 
 */
/**
 * for threadX, SPI should be in radio driver; radio driver will have high priority listen calls
 * once listen calls are moved to a large buffer, slower task will handle the emptying of that buffer
 */


int32_t Cloned_SPI_Read(Cloned_SPI * SPI_Handle, GPIO * nSS, uint8_t * Return_Data, uint16_t Return_Data_Size)
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

int32_t Cloned_SPI_Addressed_Write(Cloned_SPI * SPI_Handle, GPIO * nSS, uint8_t * Address_Data, uint16_t Address_Data_Size, uint8_t * Transmit_Data, uint16_t Transmit_Data_Size)
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

int32_t Cloned_SPI_Addressed_Read(Cloned_SPI * SPI_Handle, GPIO * nSS, uint8_t * Transmit_Data, uint16_t Transmit_Data_Size, uint8_t * Return_Data, uint16_t Return_Data_Size)
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

/* DMA FUNCTION CALLS */
int32_t Cloned_SPI_Write_DMA(Cloned_SPI * SPI_Handle, GPIO * nSS, uint8_t * Transmit_Data, uint16_t Transmit_Data_Size, void * Pre_Function_PTR, void * Post_Function_PTR, void * Function_Data)
{
	/* Save all the data and queue to be processed when the bus is free */
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
			task->Address_Data = NULL;
			task->Address_Data_Size = 0;
			task->current_phase = eSPI_Task_Phase_Single;
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

int32_t Cloned_SPI_Addressed_Write_DMA(Cloned_SPI * SPI_Handle, GPIO * nSS, uint8_t * Address_Data, uint16_t Address_Data_Size, uint8_t * Transmit_Data, uint16_t Transmit_Data_Size, void * Pre_Function_PTR, void * Post_Function_PTR, void * Function_Data)
{
	/* Save all the data and queue to be processed when the bus is free */
	SPI_Task * task = (SPI_Task *)Task_malloc(SPI_Handle->Task_ID, sizeof(SPI_Task));
	if(task != NULL)
	{
		/* Allocate memory for address data */
		task->Address_Data = (uint8_t *)Task_malloc(SPI_Handle->Task_ID, Address_Data_Size * sizeof(uint8_t));
		if(task->Address_Data != NULL)
		{
			/* Allocate memory for transmit data */
			task->Transmit_Data = (uint8_t *)Task_malloc(SPI_Handle->Task_ID, Transmit_Data_Size * sizeof(uint8_t));
			if(task->Transmit_Data != NULL)
			{
				task->Function_Data = Function_Data;
				task->Post_Function = Post_Function_PTR;
				task->Pre_Function = Pre_Function_PTR;
				
				/* Copy address and transmit data */
				memcpy(task->Address_Data, Address_Data, Address_Data_Size);
				memcpy(task->Transmit_Data, Transmit_Data, Transmit_Data_Size);
				
				task->Address_Data_Size = Address_Data_Size;
				task->Transmit_Data_Size = Transmit_Data_Size;
				task->current_phase = eSPI_Task_Phase_Address;
				task->Type = eAddressed_Write_DMA;
				task->nSS = nSS;

				Enqueue(&SPI_Handle->Task_Queue, (void *)task);

				return Transmit_Data_Size;
			}
			else
			{
				Task_free(SPI_Handle->Task_ID, task->Address_Data);
				Task_free(SPI_Handle->Task_ID, task);
			}
		}
		else
		{
			Task_free(SPI_Handle->Task_ID, task);
		}
	}

	return eSPI_Failed;
}

int32_t Cloned_SPI_Addressed_Read_Circular(Cloned_SPI * SPI_Handle, GPIO * nSS, uint8_t * Address_Data, uint16_t Address_Data_Size, uint8_t * Return_Buffer, uint16_t Expected_Read_Bytes)
{
	/* Ensure circular read is active to use the circular buffer */
	if(!SPI_Handle->circular_read_active)
	{
		return eSPI_Failed;
	}
	
	/* This is a blocking function that handles addressed read outside the task system */
	Set_GPIO_State_Low(nSS);
	
	/* First transmit the address */
	HAL_StatusTypeDef ret = HAL_SPI_Transmit(SPI_Handle->SPI_Handle, Address_Data, Address_Data_Size, MAX_SPI_WAIT_TIME);
	if(ret != HAL_OK)
	{
		Set_GPIO_State_High(nSS);
		return (ret == HAL_ERROR) ? eSPI_Failed : 
			   (ret == HAL_BUSY) ? eSPI_Busy : 
			   (ret == HAL_TIMEOUT) ? eSPI_Timeout : eSPI_Failed;
	}
	
	/* Mark the start position for reading from circular buffer */
	uint32_t start_position = SPI_Handle->dma_buffer.write_index;
	
	/* Wait for enough bytes to be received in circular buffer */
	uint32_t timeout_counter = 0;
	const uint32_t max_timeout = 10000; /* Adjust as needed */
	
	while(timeout_counter < max_timeout)
	{
		uint32_t current_write_pos = SPI_Handle->dma_buffer.write_index;
		uint32_t available_bytes;
		
		if(current_write_pos >= start_position)
		{
			available_bytes = current_write_pos - start_position;
		}
		else
		{
			/* Wrapped around */
			available_bytes = (SPI_DMA_BUFFER_SIZE - start_position) + current_write_pos;
		}
		
		if(available_bytes >= Expected_Read_Bytes)
		{
			/* Enough data available, read it */
			uint32_t bytes_read = Circular_Buffer_Read_From_Position(&SPI_Handle->dma_buffer, 
																	start_position, 
																	Return_Buffer, 
																	Expected_Read_Bytes);
			
			Set_GPIO_State_High(nSS);
			
			if(bytes_read == Expected_Read_Bytes)
			{
				return Expected_Read_Bytes;
			}
			else
			{
				return eSPI_Failed;
			}
		}
		
		timeout_counter++;
		/* Small delay to prevent busy waiting */
		/* HAL_Delay(1); // Uncomment if you want to add delay */
	}
	
	/* Timeout occurred */
	Set_GPIO_State_High(nSS);
	return eSPI_Timeout;
}

/* CIRCULAR DMA READ FUNCTIONS */
int32_t Cloned_SPI_Start_Circular_Read(Cloned_SPI * SPI_Handle, GPIO * nSS)
{
	if(!SPI_Handle || SPI_Handle->circular_read_active) 
		return eSPI_Busy;
		
	/* Initialize the circular buffer */
	Circular_Buffer_Init(&SPI_Handle->dma_buffer);
	SPI_Handle->dma_buffer.nSS = nSS;
	
	/* Set CS low to start communication */
	Set_GPIO_State_Low(nSS);
	
	/* Start circular DMA reception */
	HAL_StatusTypeDef ret = HAL_SPI_Receive_DMA(SPI_Handle->SPI_Handle, 
												(uint8_t*)SPI_Handle->dma_buffer.buffer, 
												SPI_DMA_BUFFER_SIZE);
	
	if(ret == HAL_OK)
	{
		SPI_Handle->circular_read_active = true;
		SPI_Handle->dma_buffer.state = eSPI_DMA_Active;
		return SPI_DMA_BUFFER_SIZE;
	}
	else
	{
		/* Failed to start, release CS */
		Set_GPIO_State_High(nSS);
		if(ret == HAL_ERROR)
			return eSPI_Failed;
		else if(ret == HAL_BUSY)
			return eSPI_Busy;
		else if(ret == HAL_TIMEOUT)
			return eSPI_Timeout;
		else
			return eSPI_Failed;
	}
}

int32_t Cloned_SPI_Stop_Circular_Read(Cloned_SPI * SPI_Handle)
{
	if(!SPI_Handle || !SPI_Handle->circular_read_active) 
		return eSPI_Failed;
		
	/* Stop DMA reception */
	HAL_SPI_DMAStop(SPI_Handle->SPI_Handle);
	
	/* Set CS high to end communication */
	if(SPI_Handle->dma_buffer.nSS)
		Set_GPIO_State_High(SPI_Handle->dma_buffer.nSS);
	
	/* Reset state */
	SPI_Handle->circular_read_active = false;
	SPI_Handle->dma_buffer.state = eSPI_DMA_Idle;
	
	return 0;
}

int32_t Cloned_SPI_Read_Available_Data(Cloned_SPI * SPI_Handle, uint8_t * buffer, uint32_t max_bytes)
{
	if(!SPI_Handle || !buffer || max_bytes == 0) 
		return eSPI_Failed;
		
	if(!SPI_Handle->circular_read_active)
		return 0;
		
	return Circular_Buffer_Read_Data(&SPI_Handle->dma_buffer, buffer, max_bytes);
}

bool Cloned_SPI_Is_Data_Available(Cloned_SPI * SPI_Handle)
{
	if(!SPI_Handle || !SPI_Handle->circular_read_active) 
		return false;
		
	return SPI_Handle->dma_buffer.data_available;
}

uint32_t Cloned_SPI_Get_Available_Bytes(Cloned_SPI * SPI_Handle)
{
	if(!SPI_Handle || !SPI_Handle->circular_read_active) 
		return 0;
		
	return Circular_Buffer_Used_Space(&SPI_Handle->dma_buffer);
}

/* Circular Buffer Helper Functions */
static void Circular_Buffer_Init(volatile SPI_DMA_CircularBuffer * buffer)
{
	memset((void*)buffer->buffer, 0, SPI_DMA_BUFFER_SIZE);
	buffer->write_index = 0;
	buffer->read_index = 0;
	buffer->data_available = false;
	buffer->overflow = false;
	buffer->state = eSPI_DMA_Idle;
	buffer->bytes_received = 0;
	buffer->nSS = NULL;
}

static uint32_t Circular_Buffer_Available_Space(volatile SPI_DMA_CircularBuffer * buffer)
{
	if(buffer->write_index >= buffer->read_index)
	{
		return SPI_DMA_BUFFER_SIZE - (buffer->write_index - buffer->read_index) - 1;
	}
	else
	{
		return buffer->read_index - buffer->write_index - 1;
	}
}

static uint32_t Circular_Buffer_Used_Space(volatile SPI_DMA_CircularBuffer * buffer)
{
	if(buffer->write_index >= buffer->read_index)
	{
		return buffer->write_index - buffer->read_index;
	}
	else
	{
		return SPI_DMA_BUFFER_SIZE - (buffer->read_index - buffer->write_index);
	}
}

static void Circular_Buffer_Update_Write_Index(volatile SPI_DMA_CircularBuffer * buffer, uint32_t bytes_written)
{
	buffer->write_index = (buffer->write_index + bytes_written) % SPI_DMA_BUFFER_SIZE;
	buffer->bytes_received += bytes_written;
	
	if(Circular_Buffer_Used_Space(buffer) > 0)
	{
		buffer->data_available = true;
	}
	
	/* Check for overflow */
	if(Circular_Buffer_Available_Space(buffer) == 0)
	{
		buffer->overflow = true;
	}
}

static uint32_t Circular_Buffer_Read_Data(volatile SPI_DMA_CircularBuffer * buffer, uint8_t * dest, uint32_t max_bytes)
{
	uint32_t available = Circular_Buffer_Used_Space(buffer);
	uint32_t to_read = (available < max_bytes) ? available : max_bytes;
	uint32_t bytes_read = 0;
	
	while(bytes_read < to_read)
	{
		dest[bytes_read] = buffer->buffer[buffer->read_index];
		buffer->read_index = (buffer->read_index + 1) % SPI_DMA_BUFFER_SIZE;
		bytes_read++;
	}
	
	/* Update data available flag */
	if(Circular_Buffer_Used_Space(buffer) == 0)
	{
		buffer->data_available = false;
	}
	
	/* Clear overflow flag if we've made space */
	if(buffer->overflow && Circular_Buffer_Available_Space(buffer) > 0)
	{
		buffer->overflow = false;
	}
	
	return bytes_read;
}

static uint32_t Circular_Buffer_Read_From_Position(volatile SPI_DMA_CircularBuffer * buffer, uint32_t start_pos, uint8_t * dest, uint32_t bytes_to_read)
{
	uint32_t current_write_pos = buffer->write_index;
	uint32_t available_bytes;
	uint32_t bytes_read = 0;
	uint32_t read_pos = start_pos;
	
	/* Calculate available bytes from start position to current write position */
	if(current_write_pos >= start_pos)
	{
		available_bytes = current_write_pos - start_pos;
	}
	else
	{
		/* Wrapped around */
		available_bytes = (SPI_DMA_BUFFER_SIZE - start_pos) + current_write_pos;
	}
	
	/* Read only what's available and requested */
	uint32_t to_read = (available_bytes < bytes_to_read) ? available_bytes : bytes_to_read;
	
	while(bytes_read < to_read)
	{
		dest[bytes_read] = buffer->buffer[read_pos];
		read_pos = (read_pos + 1) % SPI_DMA_BUFFER_SIZE;
		bytes_read++;
	}
	
	return bytes_read;
}


/* DMA Callback Functions */
void HAL_SPI_TxCpltCallback_Cloned(SPI_HandleTypeDef *hspi)
{
	/* Search for the correct spi handle */
	for(int c = 0; c < Cloned_SPI_Callback_Handles.Size; c++)
	{
		Cloned_SPI * spi = (Cloned_SPI *)Queue_Peek(&Cloned_SPI_Callback_Handles, c);
		if(spi->SPI_Handle == hspi)
		{
			/* We have found the spi handle the callback is for */

			/* Handle multi-phase addressed write operations */
			if(spi->Current_Task->Type == eAddressed_Write_DMA)
			{
				if(spi->Current_Task->current_phase == eSPI_Task_Phase_Address)
				{
					/* Address phase complete, now transmit data */
					spi->Current_Task->current_phase = eSPI_Task_Phase_Data;
					HAL_SPI_Transmit_DMA(spi->SPI_Handle, spi->Current_Task->Transmit_Data, spi->Current_Task->Transmit_Data_Size);
					break; /* Don't complete the task yet */
				}
				else if(spi->Current_Task->current_phase == eSPI_Task_Phase_Data)
				{
					/* Data phase complete, finish the task */
					Set_GPIO_State_High(spi->Current_Task->nSS);

					if(spi->Current_Task->Post_Function != NULL)
					{
						spi->Current_Task->Post_Function(spi->Current_Task->Function_Data);
					}

					spi->SPI_Busy = false;
				}
			}
			else
			{
				/* Single phase operation - complete normally */
				Set_GPIO_State_High(spi->Current_Task->nSS);

				/* The transmission is complete, call the post function pointer then set the bus to free */
				if(spi->Current_Task->Post_Function != NULL)
				{
					spi->Current_Task->Post_Function(spi->Current_Task->Function_Data);
				}

				spi->SPI_Busy = false;
			}
			break;
		}
	}
}

void HAL_SPI_RxCpltCallback_Cloned(SPI_HandleTypeDef *hspi)
{
	/* Search for the correct spi handle */
	for(int c = 0; c < Cloned_SPI_Callback_Handles.Size; c++)
	{
		Cloned_SPI * spi = (Cloned_SPI *)Queue_Peek(&Cloned_SPI_Callback_Handles, c);
		if(spi->SPI_Handle == hspi)
		{
			/* Handle circular DMA read completion */
			if(spi->circular_read_active)
			{
				/* Full buffer complete - update write index to full buffer */
				Circular_Buffer_Update_Write_Index(&spi->dma_buffer, SPI_DMA_BUFFER_SIZE);
				spi->dma_buffer.state = eSPI_DMA_Full_Complete;
			}
			break;
		}
	}
}


void HAL_SPI_ErrorCallback_Cloned(SPI_HandleTypeDef *hspi)
{
	/* Search for the correct spi handle */
	for(int c = 0; c < Cloned_SPI_Callback_Handles.Size; c++)
	{
		Cloned_SPI * spi = (Cloned_SPI *)Queue_Peek(&Cloned_SPI_Callback_Handles, c);
		if(spi->SPI_Handle == hspi)
		{
			/* Handle error condition */
			if(spi->circular_read_active)
			{
				spi->dma_buffer.state = eSPI_DMA_Error;
				/* Optionally restart or stop circular read on error */
				Cloned_SPI_Stop_Circular_Read(spi);
			}
			else if(spi->Current_Task)
			{
				/* Set CS high and mark as not busy */
				Set_GPIO_State_High(spi->Current_Task->nSS);
				spi->SPI_Busy = false;
			}
			break;
		}
	}
}