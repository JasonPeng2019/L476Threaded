/*
 * SPI.h
 *
 *  Created on: Aug 15, 2025
 *      Author: jason.peng (Mirrored from I2C driver)
 */

#ifndef SPI_SPI_H_
#define SPI_SPI_H_

#include "main.h"
#include "../../Middlewares/Queue/Queue.h"
#include <stdbool.h>

typedef enum {
    eSPI_Write,
    eSPI_Read,
    eSPI_WriteRead,         // Full duplex operation
    eSPI_ChipSelect_Write,  // Write with manual CS control
    eSPI_ChipSelect_Read,   // Read with manual CS control
    eSPI_ChipSelect_WriteRead, // Full duplex with manual CS control
} eSPI_Op_Type;

typedef enum {
    eSPI_Mode_Single,
    eSPI_Mode_Continuous
} eSPI_Mode;

// SPI Packet structure - handles duplex nature of SPI
typedef struct {
    eSPI_Op_Type Op_type;
    uint8_t * TX_Data;      // Data to transmit (can be NULL for read-only)
    uint8_t * RX_Data;      // Buffer for received data (can be NULL for write-only)
    uint16_t Data_Size;     // Size of data transfer
    GPIO_TypeDef * CS_Port; // Chip Select GPIO port (NULL for auto CS)
    uint16_t CS_Pin;        // Chip Select GPIO pin
    bool CS_Active_Low;     // CS polarity (true = active low, false = active high)
    void(*Complete_CallBack)(void *);
    void * CallBack_Data;
    uint8_t Tries_timeout;
    bool * Success;
} tSPI_Packet;

// SPI Continuous Channel structure
typedef struct {
    uint8_t * TX_Buffer;    // Continuous transmit buffer
    uint8_t * RX_Buffer;    // Continuous receive buffer
    uint16_t Buffer_Size;
    bool * Success;
    bool Buffer_Ready;      // Flag indicating buffer is ready for processing
    void(*Complete_CallBack)(void *);
    void * CallBack_Data;
    uint8_t Tries_timeout;
    uint32_t Transfer_Idx;  // Current transfer index
    GPIO_TypeDef * CS_Port; // Chip Select for continuous mode
    uint16_t CS_Pin;
    bool CS_Active_Low;
} tSPI_Continuous_Channel;

typedef struct {
    eSPI_Mode Mode;
    SPI_HandleTypeDef * SPI_Handle;
    volatile bool Busy_Flag;
    Queue* Packet_Queue;
    uint32_t Task_ID;
    tSPI_Continuous_Channel * Continuous_Channel;
    tSPI_Packet * Current_Packet;
    
    // SPI-specific configuration
    uint32_t Baudrate_Prescaler;    // SPI clock prescaler
    uint32_t Clock_Polarity;        // CPOL setting
    uint32_t Clock_Phase;           // CPHA setting
    uint32_t Data_Size_Config;      // 8-bit, 16-bit, etc.
    uint32_t First_Bit;             // MSB or LSB first
} tSPI;

// Initialization and configuration functions
tSPI * Init_SPI(SPI_HandleTypeDef * SPI_Handle);
void Reset_SPI(tSPI * SPI);
bool Configure_SPI_Timing(tSPI * SPI, uint32_t Baudrate_Prescaler, uint32_t Clock_Polarity, uint32_t Clock_Phase);

// Mode switching functions
bool Change_Single_Mode(tSPI * SPI);
bool Change_Continuous_Mode(tSPI * SPI, tSPI_Continuous_Channel * Channel);

// Blocking SPI operations
bool SPI_Blocking_Write(tSPI * SPI, uint8_t * Data, uint16_t Data_Size, uint32_t Timeout);
bool SPI_Blocking_Read(tSPI * SPI, uint8_t * Data, uint16_t Data_Size, uint32_t Timeout);
bool SPI_Blocking_WriteRead(tSPI * SPI, uint8_t * TX_Data, uint8_t * RX_Data, uint16_t Data_Size, uint32_t Timeout);

// Blocking SPI operations with manual Chip Select
bool SPI_Blocking_CS_Write(tSPI * SPI, uint8_t * Data, uint16_t Data_Size, GPIO_TypeDef * CS_Port, uint16_t CS_Pin, bool CS_Active_Low, uint32_t Timeout);
bool SPI_Blocking_CS_Read(tSPI * SPI, uint8_t * Data, uint16_t Data_Size, GPIO_TypeDef * CS_Port, uint16_t CS_Pin, bool CS_Active_Low, uint32_t Timeout);
bool SPI_Blocking_CS_WriteRead(tSPI * SPI, uint8_t * TX_Data, uint8_t * RX_Data, uint16_t Data_Size, GPIO_TypeDef * CS_Port, uint16_t CS_Pin, bool CS_Active_Low, uint32_t Timeout);

// Non-blocking SPI operations
bool SPI_Write(tSPI * SPI, uint8_t * Data, uint16_t Data_Size, uint8_t Tries_timeout, bool * Success);
bool SPI_Read(tSPI * SPI, uint8_t * Data, uint16_t Data_Size, uint8_t Tries_timeout, bool * Success);
bool SPI_WriteRead(tSPI * SPI, uint8_t * TX_Data, uint8_t * RX_Data, uint16_t Data_Size, uint8_t Tries_timeout, bool * Success);

// Non-blocking SPI operations with callbacks
bool SPI_Callback_Write(tSPI * SPI, uint8_t * Data, uint16_t Data_Size, uint8_t Tries_timeout, bool * Success, void (*Complete_CallBack)(void *), void * CallBack_Data);
bool SPI_Callback_Read(tSPI * SPI, uint8_t * Data, uint16_t Data_Size, uint8_t Tries_timeout, bool * Success, void (*Complete_CallBack)(void *), void * CallBack_Data);
bool SPI_Callback_WriteRead(tSPI * SPI, uint8_t * TX_Data, uint8_t * RX_Data, uint16_t Data_Size, uint8_t Tries_timeout, bool * Success, void (*Complete_CallBack)(void *), void * CallBack_Data);

// Non-blocking SPI operations with manual Chip Select
bool SPI_CS_Write(tSPI * SPI, uint8_t * Data, uint16_t Data_Size, GPIO_TypeDef * CS_Port, uint16_t CS_Pin, bool CS_Active_Low, uint8_t Tries_timeout, bool * Success);
bool SPI_CS_Read(tSPI * SPI, uint8_t * Data, uint16_t Data_Size, GPIO_TypeDef * CS_Port, uint16_t CS_Pin, bool CS_Active_Low, uint8_t Tries_timeout, bool * Success);
bool SPI_CS_WriteRead(tSPI * SPI, uint8_t * TX_Data, uint8_t * RX_Data, uint16_t Data_Size, GPIO_TypeDef * CS_Port, uint16_t CS_Pin, bool CS_Active_Low, uint8_t Tries_timeout, bool * Success);

// Task management
void SPI_Task(tSPI * SPI);

// Utility functions
void SPI_CS_Assert(GPIO_TypeDef * CS_Port, uint16_t CS_Pin, bool CS_Active_Low);
void SPI_CS_Deassert(GPIO_TypeDef * CS_Port, uint16_t CS_Pin, bool CS_Active_Low);

#endif /* SPI_SPI_H_ */