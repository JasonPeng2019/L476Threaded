/*
 * I2C.h
 *
 *  Created on: Jul 13, 2025
 *      Author: jason.peng
 */

#ifndef I2C_I2C_H_
#define I2C_I2C_H_

#include "main.h"
#include "../../Middlewares/Queue/Queue.h"
#include <stdbool.h>


typedef enum {
    eI2C_Write,
    eI2C_SingleRead,
    eI2C_MemWrite,
    eI2C_MemRead,
    eI2C_ContinuousRead,
} eOp_Type;

typedef enum {
    eMode_Single,
    eMode_Continuous
}eI2c_Mode;

// all dynamic alloc data independent of struct
typedef struct {
    eOp_Type Op_type;
	uint16_t Memory_Address;
	uint16_t Memory_Address_Size;
	uint8_t * Data;  // dynamic alloc data
	uint16_t Data_Size;
	void(*Complete_CallBack)(void *);
	void * CallBack_Data;
    uint8_t Tries_timeout;
    bool * Success;
}tI2C_Packet;

//all dynamic alloc data should be independent of struct
typedef struct {
    uint16_t Memory_Address;
    uint16_t Memory_Address_Size;
    uint8_t * Data;
    uint16_t Buffer_Size;
    bool * Success;
    bool Buffer_Ready; // when this flag is true, move data to the configured buffer.
    void(*Complete_CallBack)(void *);
	void * CallBack_Data;
    uint8_t Tries_timeout;
    uint32_t Read_Idx;
}tI2C_Continuous_Channel;
    
typedef struct {
    eI2c_Mode Mode;
    I2C_HandleTypeDef * I2C_Handle;
    volatile bool Busy_Flag;
    Queue* Packet_Queue;
    uint16_t Device_Address;
    uint32_t Task_ID;
    tI2C_Continuous_Channel * Continuous_Channel;
    tI2C_Packet * Current_Packet;
}tI2C;

tI2C * Init_I2C(I2C_HandleTypeDef * I2C_Handle, uint16_t Device_Address);
void Reset_I2C(tI2C * I2C);

bool Change_Single_Mode(tI2C * I2C);
bool Change_Continuous_Mode(tI2C * I2C, tI2C_Continuous_Channel * Channel);

bool I2C_Blocking_Write(tI2C * I2C, uint8_t * Data, uint16_t Data_Size, uint32_t Timeout);
bool I2C_Blocking_Read(tI2C * I2C, uint8_t * Data, uint16_t Data_Size, uint32_t Timeout);
bool I2C_Blocking_Memory_Write(tI2C * I2C, uint16_t Memory_Address, uint16_t Memory_Address_Size, uint8_t * Data, uint16_t Data_Size, uint32_t Timeout);
bool I2C_Blocking_Memory_Read(tI2C * I2C, uint16_t Memory_Address, uint16_t Memory_Address_Size, uint8_t * Data, uint16_t Data_Size, uint32_t Timeout);

bool I2C_Read(tI2C * I2C, uint8_t * Data, uint16_t Data_Size, uint8_t Tries_timeout, bool * Success);
bool I2C_Callback_Read(tI2C * I2C, uint8_t * Data, uint16_t Data_Size, uint8_t Tries_timeout, bool * Success, void (*Complete_CallBack)(void *), void * CallBack_Data);
bool I2C_Memory_Read(tI2C * I2C, uint16_t Memory_Address, uint16_t Memory_Address_Size, uint8_t * Data, uint16_t Data_Size, uint8_t Tries_timeout, bool * Success);
bool I2C_Memory_Write(tI2C * I2C, uint16_t Memory_Address, uint16_t Memory_Address_Size, uint8_t * Data, uint16_t Data_Size, uint8_t Tries_timeout, bool * Success);

void I2C_Task(tI2C * I2C);
#endif




