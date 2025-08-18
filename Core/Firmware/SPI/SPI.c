/***
 * author: Jason.p
 * date: 8/15/25
 * SPI Driver - Mirrored from I2C driver with SPI-specific adaptations
 */

#include "SPI.h"

tSPI * Init_SPI(SPI_HandleTypeDef * SPI_Handle){
    tSPI * SPI = (tSPI *)malloc(sizeof(tSPI));
    if (SPI == NULL){
        return NULL; 
    }
    SPI->SPI_Handle = SPI_Handle; 
    SPI->Packet_Queue = Prep_Queue();
    if (SPI->Packet_Queue == NULL){
        free(SPI);
        return NULL;
    }
    SPI->Busy_Flag = false;
    SPI->Mode = eSPI_Mode_Single;
    SPI->Task_ID = Start_Task(SPI_Task, SPI, 0);
    Set_Task_Name(SPI->Task_ID, "SPI Task");
    SPI->Continuous_Channel = NULL;
    SPI->Current_Packet = NULL;
    
    // Initialize SPI-specific configuration with sensible defaults
    SPI->Baudrate_Prescaler = SPI_BAUDRATEPRESCALER_16;
    SPI->Clock_Polarity = SPI_POLARITY_LOW;
    SPI->Clock_Phase = SPI_PHASE_1EDGE;
    SPI->Data_Size_Config = SPI_DATASIZE_8BIT;
    SPI->First_Bit = SPI_FIRSTBIT_MSB;
    
    return SPI;
}

void Reset_SPI(tSPI * SPI){
    if (SPI == NULL) return;
    
    Change_Single_Mode(SPI);  

    if (HAL_SPI_DeInit(SPI->SPI_Handle) != HAL_OK){
        return;
    }
    if (HAL_SPI_Init(SPI->SPI_Handle) != HAL_OK){
        return;
    }

    SPI->Busy_Flag = false;

    // Handle the packet queue
    while (SPI->Packet_Queue->Size > 0){
        tSPI_Packet * Curr_Packet = (tSPI_Packet *)Dequeue(SPI->Packet_Queue);
        if (Curr_Packet) {
            free(Curr_Packet);
        }
    }
    free(SPI->Packet_Queue);
    SPI->Packet_Queue = NULL;
    SPI->Packet_Queue = Prep_Queue();
    if (SPI->Packet_Queue == NULL){
        return;
    }    

    if (SPI->Task_ID != NULL){
        Halt_Task(SPI->Task_ID);
        Delete_Task(SPI->Task_ID);
    }
    SPI->Task_ID = NULL;
    SPI->Task_ID = Start_Task(SPI_Task, SPI, 0);
    Set_Task_Name(SPI->Task_ID, "SPI Task");
}

bool Configure_SPI_Timing(tSPI * SPI, uint32_t Baudrate_Prescaler, uint32_t Clock_Polarity, uint32_t Clock_Phase){
    if (SPI == NULL || SPI->SPI_Handle == NULL) return false;
    
    SPI->Baudrate_Prescaler = Baudrate_Prescaler;
    SPI->Clock_Polarity = Clock_Polarity;
    SPI->Clock_Phase = Clock_Phase;
    
    // Update HAL configuration
    SPI->SPI_Handle->Init.BaudRatePrescaler = Baudrate_Prescaler;
    SPI->SPI_Handle->Init.CLKPolarity = Clock_Polarity;
    SPI->SPI_Handle->Init.CLKPhase = Clock_Phase;
    
    return (HAL_SPI_Init(SPI->SPI_Handle) == HAL_OK);
}

bool Change_Single_Mode(tSPI * SPI){
    if (SPI == NULL) return false;
    
    if (SPI->Mode == eSPI_Mode_Single){
        return true;
    }
    else {
        SPI->Mode = eSPI_Mode_Single;
        SPI->Busy_Flag = false;
        
        // Abort any ongoing DMA operations
        if (SPI->SPI_Handle->hdmatx) {
            HAL_DMA_Abort(SPI->SPI_Handle->hdmatx);
            HAL_DMA_DeInit(SPI->SPI_Handle->hdmatx);
            HAL_DMA_Init(SPI->SPI_Handle->hdmatx);
        }
        if (SPI->SPI_Handle->hdmarx) {
            HAL_DMA_Abort(SPI->SPI_Handle->hdmarx);
            HAL_DMA_DeInit(SPI->SPI_Handle->hdmarx);
            HAL_DMA_Init(SPI->SPI_Handle->hdmarx);
        }
        
        // Clear continuous channel data if it exists
        if (SPI->Continuous_Channel && SPI->Continuous_Channel->RX_Buffer) {
            memset(SPI->Continuous_Channel->RX_Buffer, 0, SPI->Continuous_Channel->Buffer_Size);
        }
        return true;
    }
}

bool Change_Continuous_Mode(tSPI * SPI, tSPI_Continuous_Channel * Channel){
    if (SPI == NULL || Channel == NULL) return false;
    
    if (SPI->Mode == eSPI_Mode_Continuous){
        return true;
    }
    else{
        Reset_SPI(SPI);
        SPI->Mode = eSPI_Mode_Continuous;
        SPI->Continuous_Channel = Channel;
        SPI->Busy_Flag = false;
        Channel->Transfer_Idx = 0;
        return true;
    }
}

// Utility functions for Chip Select control
void SPI_CS_Assert(GPIO_TypeDef * CS_Port, uint16_t CS_Pin, bool CS_Active_Low){
    if (CS_Port == NULL) return;
    
    if (CS_Active_Low) {
        HAL_GPIO_WritePin(CS_Port, CS_Pin, GPIO_PIN_RESET);
    } else {
        HAL_GPIO_WritePin(CS_Port, CS_Pin, GPIO_PIN_SET);
    }
}

void SPI_CS_Deassert(GPIO_TypeDef * CS_Port, uint16_t CS_Pin, bool CS_Active_Low){
    if (CS_Port == NULL) return;
    
    if (CS_Active_Low) {
        HAL_GPIO_WritePin(CS_Port, CS_Pin, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(CS_Port, CS_Pin, GPIO_PIN_RESET);
    }
}

// Blocking SPI operations
bool SPI_Blocking_Write(tSPI * SPI, uint8_t * Data, uint16_t Data_Size, uint32_t Timeout){
    if (SPI == NULL || Data == NULL || SPI->SPI_Handle == NULL) return false;
    
    HAL_StatusTypeDef res = HAL_SPI_Transmit(SPI->SPI_Handle, Data, Data_Size, Timeout);
    return (res == HAL_OK);
}

bool SPI_Blocking_Read(tSPI * SPI, uint8_t * Data, uint16_t Data_Size, uint32_t Timeout){
    if (SPI == NULL || Data == NULL || SPI->SPI_Handle == NULL) return false;
    
    HAL_StatusTypeDef res = HAL_SPI_Receive(SPI->SPI_Handle, Data, Data_Size, Timeout);
    return (res == HAL_OK);
}

bool SPI_Blocking_WriteRead(tSPI * SPI, uint8_t * TX_Data, uint8_t * RX_Data, uint16_t Data_Size, uint32_t Timeout){
    if (SPI == NULL || TX_Data == NULL || RX_Data == NULL || SPI->SPI_Handle == NULL) return false;
    
    HAL_StatusTypeDef res = HAL_SPI_TransmitReceive(SPI->SPI_Handle, TX_Data, RX_Data, Data_Size, Timeout);
    return (res == HAL_OK);
}

// Blocking SPI operations with manual Chip Select
bool SPI_Blocking_CS_Write(tSPI * SPI, uint8_t * Data, uint16_t Data_Size, GPIO_TypeDef * CS_Port, uint16_t CS_Pin, bool CS_Active_Low, uint32_t Timeout){
    if (SPI == NULL || Data == NULL || CS_Port == NULL) return false;
    
    SPI_CS_Assert(CS_Port, CS_Pin, CS_Active_Low);
    bool result = SPI_Blocking_Write(SPI, Data, Data_Size, Timeout);
    SPI_CS_Deassert(CS_Port, CS_Pin, CS_Active_Low);
    return result;
}

bool SPI_Blocking_CS_Read(tSPI * SPI, uint8_t * Data, uint16_t Data_Size, GPIO_TypeDef * CS_Port, uint16_t CS_Pin, bool CS_Active_Low, uint32_t Timeout){
    if (SPI == NULL || Data == NULL || CS_Port == NULL) return false;
    
    SPI_CS_Assert(CS_Port, CS_Pin, CS_Active_Low);
    bool result = SPI_Blocking_Read(SPI, Data, Data_Size, Timeout);
    SPI_CS_Deassert(CS_Port, CS_Pin, CS_Active_Low);
    return result;
}

bool SPI_Blocking_CS_WriteRead(tSPI * SPI, uint8_t * TX_Data, uint8_t * RX_Data, uint16_t Data_Size, GPIO_TypeDef * CS_Port, uint16_t CS_Pin, bool CS_Active_Low, uint32_t Timeout){
    if (SPI == NULL || TX_Data == NULL || RX_Data == NULL || CS_Port == NULL) return false;
    
    SPI_CS_Assert(CS_Port, CS_Pin, CS_Active_Low);
    bool result = SPI_Blocking_WriteRead(SPI, TX_Data, RX_Data, Data_Size, Timeout);
    SPI_CS_Deassert(CS_Port, CS_Pin, CS_Active_Low);
    return result;
}

// Non-blocking SPI operations
bool SPI_Write(tSPI * SPI, uint8_t * Data, uint16_t Data_Size, uint8_t Tries_timeout, bool * Success){
    if (SPI == NULL || Data == NULL || Success == NULL) return false;
    
    tSPI_Packet * Packet = (tSPI_Packet *)malloc(sizeof(tSPI_Packet));
    if (Packet == NULL){
        return false;
    }
    Packet->Op_type = eSPI_Write;
    Packet->TX_Data = Data;
    Packet->RX_Data = NULL;
    Packet->Data_Size = Data_Size;
    Packet->CS_Port = NULL;
    Packet->CS_Pin = 0;
    Packet->CS_Active_Low = true;
    Packet->Complete_CallBack = NULL;
    Packet->CallBack_Data = NULL;
    Packet->Tries_timeout = Tries_timeout;
    Packet->Success = Success;
    
    if (Enqueue(SPI->Packet_Queue, (void *)Packet)){
        return true;
    }
    else {
        free(Packet);
        return false;
    }
}

bool SPI_Read(tSPI * SPI, uint8_t * Data, uint16_t Data_Size, uint8_t Tries_timeout, bool * Success){
    if (SPI == NULL || Data == NULL || Success == NULL) return false;
    
    tSPI_Packet * Packet = (tSPI_Packet *)malloc(sizeof(tSPI_Packet));
    if (Packet == NULL){
        return false;
    }
    Packet->Op_type = eSPI_Read;
    Packet->TX_Data = NULL;
    Packet->RX_Data = Data;
    Packet->Data_Size = Data_Size;
    Packet->CS_Port = NULL;
    Packet->CS_Pin = 0;
    Packet->CS_Active_Low = true;
    Packet->Complete_CallBack = NULL;
    Packet->CallBack_Data = NULL;
    Packet->Tries_timeout = Tries_timeout;
    Packet->Success = Success;
    
    if (Enqueue(SPI->Packet_Queue, (void *)Packet)){
        return true;
    }
    else {
        free(Packet);
        return false;
    }
}

bool SPI_WriteRead(tSPI * SPI, uint8_t * TX_Data, uint8_t * RX_Data, uint16_t Data_Size, uint8_t Tries_timeout, bool * Success){
    if (SPI == NULL || TX_Data == NULL || RX_Data == NULL || Success == NULL) return false;
    
    tSPI_Packet * Packet = (tSPI_Packet *)malloc(sizeof(tSPI_Packet));
    if (Packet == NULL){
        return false;
    }
    Packet->Op_type = eSPI_WriteRead;
    Packet->TX_Data = TX_Data;
    Packet->RX_Data = RX_Data;
    Packet->Data_Size = Data_Size;
    Packet->CS_Port = NULL;
    Packet->CS_Pin = 0;
    Packet->CS_Active_Low = true;
    Packet->Complete_CallBack = NULL;
    Packet->CallBack_Data = NULL;
    Packet->Tries_timeout = Tries_timeout;
    Packet->Success = Success;
    
    if (Enqueue(SPI->Packet_Queue, (void *)Packet)){
        return true;
    }
    else {
        free(Packet);
        return false;
    }
}

// Non-blocking SPI operations with callbacks
bool SPI_Callback_Write(tSPI * SPI, uint8_t * Data, uint16_t Data_Size, uint8_t Tries_timeout, bool * Success, void (*Complete_CallBack)(void *), void * CallBack_Data){
    if (SPI == NULL || Data == NULL || Success == NULL) return false;
    
    tSPI_Packet * Packet = (tSPI_Packet *)malloc(sizeof(tSPI_Packet));
    if (Packet == NULL){
        return false;
    }
    Packet->Op_type = eSPI_Write;
    Packet->TX_Data = Data;
    Packet->RX_Data = NULL;
    Packet->Data_Size = Data_Size;
    Packet->CS_Port = NULL;
    Packet->CS_Pin = 0;
    Packet->CS_Active_Low = true;
    Packet->Complete_CallBack = Complete_CallBack;
    Packet->CallBack_Data = CallBack_Data;
    Packet->Tries_timeout = Tries_timeout;
    Packet->Success = Success;
    
    if (Enqueue(SPI->Packet_Queue, (void *)Packet)){
        return true;
    }
    else {
        free(Packet);
        return false;
    }
}

bool SPI_Callback_Read(tSPI * SPI, uint8_t * Data, uint16_t Data_Size, uint8_t Tries_timeout, bool * Success, void (*Complete_CallBack)(void *), void * CallBack_Data){
    if (SPI == NULL || Data == NULL || Success == NULL) return false;
    
    tSPI_Packet * Packet = (tSPI_Packet *)malloc(sizeof(tSPI_Packet));
    if (Packet == NULL){
        return false;
    }
    Packet->Op_type = eSPI_Read;
    Packet->TX_Data = NULL;
    Packet->RX_Data = Data;
    Packet->Data_Size = Data_Size;
    Packet->CS_Port = NULL;
    Packet->CS_Pin = 0;
    Packet->CS_Active_Low = true;
    Packet->Complete_CallBack = Complete_CallBack;
    Packet->CallBack_Data = CallBack_Data;
    Packet->Tries_timeout = Tries_timeout;
    Packet->Success = Success;
    
    if (Enqueue(SPI->Packet_Queue, (void *)Packet)){
        return true;
    }
    else {
        free(Packet);
        return false;
    }
}

bool SPI_Callback_WriteRead(tSPI * SPI, uint8_t * TX_Data, uint8_t * RX_Data, uint16_t Data_Size, uint8_t Tries_timeout, bool * Success, void (*Complete_CallBack)(void *), void * CallBack_Data){
    if (SPI == NULL || TX_Data == NULL || RX_Data == NULL || Success == NULL) return false;
    
    tSPI_Packet * Packet = (tSPI_Packet *)malloc(sizeof(tSPI_Packet));
    if (Packet == NULL){
        return false;
    }
    Packet->Op_type = eSPI_WriteRead;
    Packet->TX_Data = TX_Data;
    Packet->RX_Data = RX_Data;
    Packet->Data_Size = Data_Size;
    Packet->CS_Port = NULL;
    Packet->CS_Pin = 0;
    Packet->CS_Active_Low = true;
    Packet->Complete_CallBack = Complete_CallBack;
    Packet->CallBack_Data = CallBack_Data;
    Packet->Tries_timeout = Tries_timeout;
    Packet->Success = Success;
    
    if (Enqueue(SPI->Packet_Queue, (void *)Packet)){
        return true;
    }
    else {
        free(Packet);
        return false;
    }
}

// Non-blocking SPI operations with manual Chip Select
bool SPI_CS_Write(tSPI * SPI, uint8_t * Data, uint16_t Data_Size, GPIO_TypeDef * CS_Port, uint16_t CS_Pin, bool CS_Active_Low, uint8_t Tries_timeout, bool * Success){
    if (SPI == NULL || Data == NULL || CS_Port == NULL || Success == NULL) return false;
    
    tSPI_Packet * Packet = (tSPI_Packet *)malloc(sizeof(tSPI_Packet));
    if (Packet == NULL){
        return false;
    }
    Packet->Op_type = eSPI_ChipSelect_Write;
    Packet->TX_Data = Data;
    Packet->RX_Data = NULL;
    Packet->Data_Size = Data_Size;
    Packet->CS_Port = CS_Port;
    Packet->CS_Pin = CS_Pin;
    Packet->CS_Active_Low = CS_Active_Low;
    Packet->Complete_CallBack = NULL;
    Packet->CallBack_Data = NULL;
    Packet->Tries_timeout = Tries_timeout;
    Packet->Success = Success;
    
    if (Enqueue(SPI->Packet_Queue, (void *)Packet)){
        return true;
    }
    else {
        free(Packet);
        return false;
    }
}

bool SPI_CS_Read(tSPI * SPI, uint8_t * Data, uint16_t Data_Size, GPIO_TypeDef * CS_Port, uint16_t CS_Pin, bool CS_Active_Low, uint8_t Tries_timeout, bool * Success){
    if (SPI == NULL || Data == NULL || CS_Port == NULL || Success == NULL) return false;
    
    tSPI_Packet * Packet = (tSPI_Packet *)malloc(sizeof(tSPI_Packet));
    if (Packet == NULL){
        return false;
    }
    Packet->Op_type = eSPI_ChipSelect_Read;
    Packet->TX_Data = NULL;
    Packet->RX_Data = Data;
    Packet->Data_Size = Data_Size;
    Packet->CS_Port = CS_Port;
    Packet->CS_Pin = CS_Pin;
    Packet->CS_Active_Low = CS_Active_Low;
    Packet->Complete_CallBack = NULL;
    Packet->CallBack_Data = NULL;
    Packet->Tries_timeout = Tries_timeout;
    Packet->Success = Success;
    
    if (Enqueue(SPI->Packet_Queue, (void *)Packet)){
        return true;
    }
    else {
        free(Packet);
        return false;
    }
}

bool SPI_CS_WriteRead(tSPI * SPI, uint8_t * TX_Data, uint8_t * RX_Data, uint16_t Data_Size, GPIO_TypeDef * CS_Port, uint16_t CS_Pin, bool CS_Active_Low, uint8_t Tries_timeout, bool * Success){
    if (SPI == NULL || TX_Data == NULL || RX_Data == NULL || CS_Port == NULL || Success == NULL) return false;
    
    tSPI_Packet * Packet = (tSPI_Packet *)malloc(sizeof(tSPI_Packet));
    if (Packet == NULL){
        return false;
    }
    Packet->Op_type = eSPI_ChipSelect_WriteRead;
    Packet->TX_Data = TX_Data;
    Packet->RX_Data = RX_Data;
    Packet->Data_Size = Data_Size;
    Packet->CS_Port = CS_Port;
    Packet->CS_Pin = CS_Pin;
    Packet->CS_Active_Low = CS_Active_Low;
    Packet->Complete_CallBack = NULL;
    Packet->CallBack_Data = NULL;
    Packet->Tries_timeout = Tries_timeout;
    Packet->Success = Success;
    
    if (Enqueue(SPI->Packet_Queue, (void *)Packet)){
        return true;
    }
    else {
        free(Packet);
        return false;
    }
}

// Main SPI Task - Core state machine
void SPI_Task(tSPI * SPI){
    if (SPI == NULL || SPI->Busy_Flag){
        return;
    }
    
    if (SPI->Mode == eSPI_Mode_Single){
        if (SPI->Current_Packet == NULL){
            if (SPI->Packet_Queue->Size > 0){
                SPI->Busy_Flag = true;
                SPI->Current_Packet = (tSPI_Packet *)Dequeue(SPI->Packet_Queue);
                SPI->Busy_Flag = false;
            }
        } else {
            SPI->Busy_Flag = true;
            static uint8_t SPI_Single_Attempts = 0;
            
            if (SPI_Single_Attempts < SPI->Current_Packet->Tries_timeout){
                SPI_Single_Attempts++;
                bool op_flag = false;
                HAL_StatusTypeDef status = HAL_ERROR;
                
                // Handle CS assertion if required
                if (SPI->Current_Packet->CS_Port != NULL) {
                    SPI_CS_Assert(SPI->Current_Packet->CS_Port, SPI->Current_Packet->CS_Pin, SPI->Current_Packet->CS_Active_Low);
                }
                
                switch (SPI->Current_Packet->Op_type){
                    case eSPI_Write:
                    case eSPI_ChipSelect_Write:
                        if (SPI->Current_Packet->TX_Data != NULL) {
                            status = HAL_SPI_Transmit_DMA(SPI->SPI_Handle, SPI->Current_Packet->TX_Data, SPI->Current_Packet->Data_Size);
                        }
                        break;
                        
                    case eSPI_Read:
                    case eSPI_ChipSelect_Read:
                        if (SPI->Current_Packet->RX_Data != NULL) {
                            status = HAL_SPI_Receive_DMA(SPI->SPI_Handle, SPI->Current_Packet->RX_Data, SPI->Current_Packet->Data_Size);
                        }
                        break;
                        
                    case eSPI_WriteRead:
                    case eSPI_ChipSelect_WriteRead:
                        if (SPI->Current_Packet->TX_Data != NULL && SPI->Current_Packet->RX_Data != NULL) {
                            status = HAL_SPI_TransmitReceive_DMA(SPI->SPI_Handle, SPI->Current_Packet->TX_Data, SPI->Current_Packet->RX_Data, SPI->Current_Packet->Data_Size);
                        }
                        break;
                        
                    default:
                        status = HAL_ERROR;
                        break;
                }
                
                if (status == HAL_OK){
                    op_flag = true;
                }
                
                // Handle CS deassertion if required
                if (SPI->Current_Packet->CS_Port != NULL) {
                    SPI_CS_Deassert(SPI->Current_Packet->CS_Port, SPI->Current_Packet->CS_Pin, SPI->Current_Packet->CS_Active_Low);
                }

                if (op_flag){
                    SPI_Single_Attempts = 0;
                    SPI->Busy_Flag = false;
                    if (SPI->Current_Packet->Success) {
                        *(SPI->Current_Packet->Success) = true;
                    }
                    if (SPI->Current_Packet->Complete_CallBack != NULL){
                        SPI->Current_Packet->Complete_CallBack(SPI->Current_Packet->CallBack_Data);
                    }
                    free(SPI->Current_Packet);
                    SPI->Current_Packet = NULL;
                }
            } else {
                // Exceeded retry attempts
                SPI_Single_Attempts = 0;
                SPI->Busy_Flag = false;
                if (SPI->Current_Packet->Success) {
                    *(SPI->Current_Packet->Success) = false;
                }
                free(SPI->Current_Packet);
                SPI->Current_Packet = NULL;
            }
        }
    } else if (SPI->Mode == eSPI_Mode_Continuous){
        // Implement continuous mode if needed in the future
        // This would handle continuous DMA transfers for streaming applications
    }
}