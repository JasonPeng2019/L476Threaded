/*
 * SPI.h
 *
 *  Created on: Apr , 2020
 *      Author: jason.peng
 */

#ifndef SPI_SPI_H_
#define SPI_SPI_H_

#include "main.h"
#include "GPIO/GPIO.h"
#include "Queue/Queue.h"
#include <stdint.h>
#include <stdbool.h>


#define MAX_SPI_WAIT_TIME		100

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
	eRead_DMA,
	eAddressed_Write_DMA,
	eAddressed_Read_DMA
}SPI_Task_Types;

typedef struct
{
	SPI_Task_Types Type;
	GPIO * nSS;
	uint8_t * Transmit_Data;
	uint32_t Transmit_Data_Size;
	void (*Pre_Function)(void *);
	void (*Post_Function)(void *);
	void * Function_Data;
}SPI_Task;

typedef struct
{
	SPI_HandleTypeDef * SPI_Handle;

	Queue Task_Queue;
	volatile bool SPI_Busy;
	SPI_Task * Current_Task;

	uint32_t Task_ID;
}SPI;

SPI * Init_SPI(SPI_HandleTypeDef * SPI_Handle);

/* BLOCKING FUNCTION CALLS */
int32_t SPI_Write(SPI * SPI_Handle, GPIO * nSS, uint8_t * Transmit_Data, uint32_t Transmit_Data_Size);
int32_t SPI_Read(SPI * SPI_Handle, GPIO * nSS, uint8_t * Return_Data, uint16_t Return_Data_Size);
int32_t SPI_Addressed_Write(SPI * SPI_Handle, GPIO * nSS, uint8_t * Address_Data, uint16_t Address_Data_Size, uint8_t * Transmit_Data, uint16_t Transmit_Data_Size);
int32_t SPI_Addressed_Read(SPI * SPI_Handle, GPIO * nSS, uint8_t * Transmit_Data, uint16_t Transmit_Data_Size, uint8_t * Return_Data, uint16_t Return_Data_Size);

/* DMA FUNCTION CALLS */
int32_t SPI_Write_DMA(SPI * SPI_Handle, GPIO * nSS, uint8_t * Transmit_Data, uint16_t Transmit_Data_Size, void * Pre_Function_PTR, void * Post_Function_PTR, void * Function_Data);


#ifdef __cplusplus
}
#endif

#endif /* SPI_SPI_H_ */
