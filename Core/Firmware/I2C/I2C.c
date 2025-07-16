/***
 * author: Jason.p
 * date: 7/16/25
 */

#include "I2C.h"
#include "../../Middlewares/Scheduler/Scheduler.h"

tI2C * Init_I2C(I2C_HandleTypeDef * I2C_Handle, uint16_t Device_Address){
    tI2C * I2C = (tI2C *)malloc(sizeof(tI2C));
    if (I2C == NULL){
        return NULL; 
    }
    I2C->I2C_Handle = I2C_Handle; 
    I2C->Packet_Queue = Prep_Queue();
    I2C->Device_Address = Device_Address;
    I2C->Busy_Flag = false;
    I2C->Mode = eMode_Single;
    I2C->Task_ID = Start_Task(I2C_Task, I2C, 0);
    Set_Task_Name(I2C->Task_ID, "I2C Task"); // refer to I2C->Task_ID for task and match, don't peek name
    I2C->Continuous_Channel = NULL;
    return I2C;
}

bool Reset_I2C(tI2C * I2C){
    HAL_I2C_DeInit(I2C->I2C_Handle);
	HAL_I2C_Init(I2C->I2C_Handle);

    I2C->Busy_Flag = false;

    free(I2C->Packet_Queue);
    I2C->Packet_Queue = NULL;
    I2C->Packet_Queue = Prep_Queue();
    
    Change_Single_Mode(I2C);
    memset(I2C->Continuous_Channel->Data, 0, *I2C->Continuous_Channel->Buffer_Size);
    free(I2C->Continuous_Channel);
    I2C->Continuous_Channel = NULL;

    if (I2C->Task_ID != NULL){
        Halt_Task(I2C->Task_ID);
        Delete_Task(I2C->Task_ID);
    }
    I2C->Task_ID = NULL;
    I2C->Task_ID = Start_Task(I2C_Task, I2C, 0);
    Set_Task_Name(I2C->Task_ID, "I2C Task"); // refer to I2C->Task_ID for task and match, don't peek name

    if (I2C->Task_ID != NULL){
        return true;
    }
    else {
        return false; //(need to try again)
    }
}

bool Change_Single_Mode(tI2C * I2C){
    if (I2C->Mode == eMode_Single){
        return true;
    }
    else {
        I2C->Mode = eMode_Single;
        I2C->Busy_Flag = false;
        
    }
}


bool I2C_Blocking_Write(tI2C * I2C, uint8_t * Data, uint16_t Data_Size, uint32_t Timeout){
    HAL_StatusTypeDef res = HAL_I2C_Master_Transmit(I2C->I2C_Handle, I2C->Device_Address, Data, Data_Size, Timeout);
    if (res != HAL_OK){
        return false;
    }
    else {
        return true;
    }
}

bool I2C_Blocking_Read(tI2C * I2C, uint8_t * Data, uint16_t Data_Size, uint32_t Timeout){
    HAL_StatusTypeDef res = HAL_I2C_Master_Receive(I2C->I2C_Handle, I2C->Device_Address, Data, Data_Size, Timeout);
    if (res != HAL_OK){
        return false;
    }
    else {
        return true;
    }
}

bool I2C_Blocking_Memory_Write(tI2C * I2C, uint16_t Memory_Address, uint16_t Memory_Address_Size, uint8_t * Data, uint16_t Data_Size, uint32_t Timeout){
    HAL_StatusTypeDef res = HAL_I2C_Mem_Write(I2C->I2C_Handle, I2C->Device_Address, Memory_Address, Memory_Address_Size, Data, Data_Size, Timeout);
    if (res != HAL_OK){
        return false;
    }
    else {
        return true;
    }
}

bool I2C_Blocking_Memory_Read(tI2C * I2C, uint16_t Memory_Address, uint16_t Memory_Address_Size, uint8_t * Data, uint16_t Data_Size, uint32_t Timeout){
    HAL_StatusTypeDef res = HAL_I2C_Mem_Read(I2C->I2C_Handle, I2C->Device_Address, Memory_Address, Memory_Address_Size, Data, Data_Size, Timeout);
    if (res != HAL_OK){
        return false;
    }
    else {
        return true;
    }
}

bool I2C_Read(tI2C * I2C, uint8_t * Data, uint16_t Data_Size, uint8_t Tries_timeout, bool * Success){
    tI2C_Packet * Packet = (tI2C_Packet *)malloc(sizeof(tI2C_Packet));
    if (Packet == NULL){
        return false;
    }
    Packet->Op_type = eI2C_Read;
    Packet->Data = Data;
    Packet->Data_Size = Data_Size;
    Packet->Complete_CallBack = NULL;
    Packet->CallBack_Data = NULL;
    Packet->Tries_timeout = Tries_timeout;
    Packet->Success = Success;
    if (Enqueue(I2C->Packet_Queue, (void *)Packet)){
        return true;
    }
    else {
        return false;
    }

}


bool I2C_Callback_Read(tI2C * I2C, uint8_t * Data, uint16_t Data_Size, uint8_t Tries_timeout, bool * Success, void (*Complete_CallBack)(void *), void * CallBack_Data){
    tI2C_Packet * Packet = (tI2C_Packet *)malloc(sizeof(tI2C_Packet));
    if (Packet == NULL){
        return false;
    }
    Packet->Op_type = eI2C_Read;
    Packet->Data = Data;
    Packet->Data_Size = Data_Size;
    Packet->Complete_CallBack = NULL;
    Packet->CallBack_Data = NULL;
    Packet->Tries_timeout = Tries_timeout;
    Packet->Success = Success;
    Packet->Complete_CallBack = Complete_CallBack;
    Packet->CallBack_Data = CallBack_Data;
    if (Enqueue(I2C->Packet_Queue, (void *)Packet)){
        return true;
    }
    else { 
        return false;
    }
}

bool I2C_Memory_Read(tI2C * I2C, uint16_t Memory_Address, uint16_t Memory_Address_Size, uint8_t * Data, uint16_t Data_Size, uint8_t Tries_timeout, bool * Success){
    tI2C_Packet * Packet = (tI2C_Packet *)malloc(sizeof(tI2C_Packet));
    if (Packet == NULL){
        return false;
    }
    Packet->Op_type = eI2C_MemRead;
    Packet->Memory_Address = Memory_Address;;
    Packet->Memory_Address_Size = Memory_Address_Size;
    Packet->Data = Data;
    Packet->Data_Size = Data_Size;
    Packet->Complete_CallBack = NULL;
    Packet->CallBack_Data = NULL;
    Packet->Tries_timeout = Tries_timeout;
    Packet->Success = Success;
    if (Enqueue(I2C->Packet_Queue, (void *)Packet)){
        return true;
    }
    else { 
        return false;
    }
}

bool I2C_Memory_Write(tI2C * I2C, uint16_t Memory_Address, uint16_t Memory_Address_Size, uint8_t * Data, uint16_t Data_Size, uint8_t Tries_timeout, bool * Success);

void I2C_Task(tI2C * I2C){
    if (I2C->Packet_Queue->Size > 0){
        if (I2C->Busy_Flag){
            return;
        }
        tI2C_Packet * Packet = (tI2C_Packet *)Dequeue(I2C->Packet_Queue);
        I2C->Busy_Flag = true;
        switch (Packet->Op_type){
            case eI2C_Read:
                static uint8_t I2C_Read_Attempts = 0;
                if (I2C_Read_Attempts < Packet->Tries_timeout){
                    I2C_Read_Attempts++;
                    if (HAL_I2C_Master_Receive_DMA(I2C->I2C_Handle, I2C->Device_Address, Packet->Data, Packet->Data_Size) == HAL_OK){
                        I2C_Read_Attempts = 0;
                        I2C->Busy_Flag = false;
                        Packet->Success = true;
                        if (Packet->Complete_CallBack != NULL){
                            Packet->Complete_CallBack(Packet->CallBack_Data);
                        }
                        free(Packet);
                    }
                } else {
                    I2C_Read_Attempts = 0;
                    I2C->Busy_Flag = false;
                    Packet->Success = false;
                    free(Packet);
                }
                break;
            case eI2C_MemWrite:
                break;
            case eI2C_MemRead:
                static uint8_t I2C_Read_Attempts = 0;
                if (I2C_Read_Attempts < Packet->Tries_timeout){
                    I2C_Read_Attempts++;
                    if (HAL_I2C_Master_Receive_DMA(I2C->I2C_Handle, I2C->Device_Address, Packet->Data, Packet->Data_Size) == HAL_OK){
                        I2C_Read_Attempts = 0;
                        I2C->Busy_Flag = false;
                        Packet->Success = true;
                        if (Packet->Complete_CallBack != NULL){
                            Packet->Complete_CallBack(Packet->CallBack_Data);
                        }
                        free(Packet);
                    }
                } else {
                    I2C_Read_Attempts = 0;
                    I2C->Busy_Flag = false;
                    Packet->Success = false;
                    free(Packet);
                }
                break;       
        }
    }
}


