/*
 * cloned_SPI.h
 *
 *  Created on: Aug 19, 2025
 *      Author: Claude Code - Enhanced SPI with Circular DMA Read
 */

#ifndef SPI_CLONED_SPI_H_
#define SPI_CLONED_SPI_H_

#include "main.h"
#include "GPIO/GPIO.h"
#include "Queue/Queue.h"
#include <stdint.h>
#include <stdbool.h>

#define MAX_SPI_WAIT_TIME		100
#define SPI_DMA_BUFFER_SIZE		1024
#define SPI_DMA_HALF_BUFFER	(SPI_DMA_BUFFER_SIZE / 2)

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
	eSPI_Failed = -1,
	eSPI_Busy = -2,
	eSPI_Timeout = -3
}SPI_Error;

typedef enum
{
	eWrite_DMA = 0,
	eAddressed_Write_DMA
}SPI_Task_Types;

typedef enum
{
	eSPI_DMA_Idle = 0,
	eSPI_DMA_Active,
	eSPI_DMA_Half_Complete,
	eSPI_DMA_Full_Complete,
	eSPI_DMA_Error
}SPI_DMA_State;

typedef enum
{
	eSPI_Task_Phase_Single = 0,
	eSPI_Task_Phase_Address,
	eSPI_Task_Phase_Data
}SPI_Task_Phase;

typedef struct
{
	SPI_Task_Types Type;
	GPIO * nSS;
	uint8_t * Transmit_Data;
	uint32_t Transmit_Data_Size;
	uint8_t * Address_Data;
	uint32_t Address_Data_Size;
	SPI_Task_Phase current_phase;
	void (*Pre_Function)(void *);
	void (*Post_Function)(void *);
	void * Function_Data;
}SPI_Task;

typedef struct
{
	uint8_t buffer[SPI_DMA_BUFFER_SIZE];
	volatile uint32_t write_index;
	volatile uint32_t read_index;
	volatile bool data_available;
	volatile bool overflow;
	volatile SPI_DMA_State state;
	volatile uint32_t bytes_received;
	GPIO * nSS;
}SPI_DMA_CircularBuffer;

typedef struct
{
	SPI_HandleTypeDef * SPI_Handle;
	DMA_HandleTypeDef * DMA_RX_Handle;
	DMA_HandleTypeDef * DMA_TX_Handle;

	Queue Task_Queue;
	volatile bool SPI_Busy;
	SPI_Task * Current_Task;

	SPI_DMA_CircularBuffer dma_buffer;
	volatile bool circular_read_active;

	uint32_t Task_ID;
}Cloned_SPI;

/* Initialization */
Cloned_SPI * Init_Cloned_SPI(SPI_HandleTypeDef * SPI_Handle, DMA_HandleTypeDef * DMA_RX_Handle, DMA_HandleTypeDef * DMA_TX_Handle);
void Cleanup_Cloned_SPI(Cloned_SPI * SPI_Handle);

/* BLOCKING FUNCTION CALLS */
int32_t Cloned_SPI_Write(Cloned_SPI * SPI_Handle, GPIO * nSS, uint8_t * Transmit_Data, uint32_t Transmit_Data_Size);
int32_t Cloned_SPI_Read(Cloned_SPI * SPI_Handle, GPIO * nSS, uint8_t * Return_Data, uint16_t Return_Data_Size);
int32_t Cloned_SPI_Addressed_Write(Cloned_SPI * SPI_Handle, GPIO * nSS, uint8_t * Address_Data, uint16_t Address_Data_Size, uint8_t * Transmit_Data, uint16_t Transmit_Data_Size);
int32_t Cloned_SPI_Addressed_Read(Cloned_SPI * SPI_Handle, GPIO * nSS, uint8_t * Transmit_Data, uint16_t Transmit_Data_Size, uint8_t * Return_Data, uint16_t Return_Data_Size);

/* DMA FUNCTION CALLS - TRANSMIT ONLY */
int32_t Cloned_SPI_Write_DMA(Cloned_SPI * SPI_Handle, GPIO * nSS, uint8_t * Transmit_Data, uint16_t Transmit_Data_Size, void * Pre_Function_PTR, void * Post_Function_PTR, void * Function_Data);
int32_t Cloned_SPI_Addressed_Write_DMA(Cloned_SPI * SPI_Handle, GPIO * nSS, uint8_t * Address_Data, uint16_t Address_Data_Size, uint8_t * Transmit_Data, uint16_t Transmit_Data_Size, void * Pre_Function_PTR, void * Post_Function_PTR, void * Function_Data);

/* CIRCULAR DMA READ FUNCTIONS */
int32_t Cloned_SPI_Start_Circular_Read(Cloned_SPI * SPI_Handle, GPIO * nSS);
int32_t Cloned_SPI_Stop_Circular_Read(Cloned_SPI * SPI_Handle);
int32_t Cloned_SPI_Read_Available_Data(Cloned_SPI * SPI_Handle, uint8_t * buffer, uint32_t max_bytes);
bool Cloned_SPI_Is_Data_Available(Cloned_SPI * SPI_Handle);
uint32_t Cloned_SPI_Get_Available_Bytes(Cloned_SPI * SPI_Handle);

/* ADDRESSED READ WITH CIRCULAR BUFFER */
int32_t Cloned_SPI_Addressed_Read_Circular(Cloned_SPI * SPI_Handle, GPIO * nSS, uint8_t * Address_Data, uint16_t Address_Data_Size, uint8_t * Return_Buffer, uint16_t Expected_Read_Bytes);

/* DMA Callback Functions */
void HAL_SPI_TxCpltCallback_Cloned(SPI_HandleTypeDef *hspi);
void HAL_SPI_RxCpltCallback_Cloned(SPI_HandleTypeDef *hspi);
void HAL_SPI_ErrorCallback_Cloned(SPI_HandleTypeDef *hspi);

#ifdef __cplusplus
}
#endif

#endif /* SPI_CLONED_SPI_H_ */