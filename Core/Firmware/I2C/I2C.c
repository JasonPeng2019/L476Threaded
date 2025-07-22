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
    I2C->Current_Packet = NULL;
    return I2C;
}

void Reset_I2C(tI2C * I2C){
    Change_Single_Mode(I2C);  

    if (!HAL_I2C_DeInit(I2C->I2C_Handle)){
        return;
    }
	if (!HAL_I2C_Init(I2C->I2C_Handle)){
        return;
    }

    I2C->Busy_Flag = false;

    //handle the packet queue
    while (I2C->Packet_Queue->Size > 0){
        tI2C_Packet * Curr_Packet = (tI2C_Packet *)Dequeue(I2C->Packet_Queue);
        free(Curr_Packet);
        // curr_packet data and success flag will be freed by drivers independently
        //i.e. if (flag_indic == true; else, free(data and flag))
    }
    free(I2C->Packet_Queue);
    I2C->Packet_Queue = NULL;
    I2C->Packet_Queue = Prep_Queue();
    if (I2C->Packet_Queue == NULL){
        return;
    }    

    if (I2C->Task_ID != NULL){
        Halt_Task(I2C->Task_ID);
        Delete_Task(I2C->Task_ID);
    }
    I2C->Task_ID = NULL;
    I2C->Task_ID = Start_Task(I2C_Task, I2C, 0);
    Set_Task_Name(I2C->Task_ID, "I2C Task"); // refer to I2C->Task_ID for task and match, don't peek name

    if (I2C->Task_ID != NULL){
        return;
    }
    else {
        return; //(need to try again)
    }
}

bool Change_Single_Mode(tI2C * I2C){
    if (I2C->Mode == eMode_Single){
        return true;
    }
    else {
        I2C->Mode = eMode_Single;
        I2C->Busy_Flag = false;
        
        HAL_DMA_Abort(I2C->I2C_Handle->hdmatx);
        HAL_DMA_Abort(I2C->I2C_Handle->hdmarx);
        HAL_I2C_DMAStop(I2C->I2C_Handle);
        HAL_DMA_DeInit(I2C->I2C_Handle->hdmarx);
        HAL_DMA_DeInit(I2C->I2C_Handle->hdmatx);

        HAL_DMA_Init(I2C->I2C_Handle->hdmarx);
        HAL_DMA_Init(I2C->I2C_Handle->hdmatx);
        __HAL_LINKDMA(I2C->I2C_Handle, hdmarx, *I2C->I2C_Handle->hdmarx);
        __HAL_LINKDMA(I2C->I2C_Handle, hdmatx, *I2C->I2C_Handle->hdmatx);

        __HAL_I2C_ENABLE_DMA(I2C->I2C_Handle);

        memset(I2C->Continuous_Channel->Data, 0, I2C->Continuous_Channel->Buffer_Size);
        return true;
    }
}

bool Change_Continuous_Mode(tI2C * I2C, tI2C_Continuous_Channel * Channel){
    if (I2C->Mode == eMode_Continuous){
        return true;
    }
    else{
        Reset_I2C(I2C);
        I2C->Mode = eMode_Continuous;
        I2C->Continuous_Channel = Channel;
        I2C->Busy_Flag = false;
        I2C->Packet_Queue = Prep_Queue();
        if (I2C->Packet_Queue == NULL){
            return false;
        }
        return true;
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

bool I2C_Memory_Write(tI2C * I2C, uint16_t Memory_Address, uint16_t Memory_Address_Size, uint8_t * Data, uint16_t Data_Size, uint8_t Tries_timeout, bool * Success){
    tI2C_Packet * Packet = (tI2C_Packet *)malloc(sizeof(tI2C_Packet));
    if (Packet == NULL){
        return false;
    }
    Packet->Op_type = eI2C_MemWrite;
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

void I2C_Task(tI2C * I2C){
    if (I2C->Busy_Flag){
        return;
    }
    if (I2C->Mode == eMode_Single){
        if (I2C->Current_Packet == NULL){
            if (I2C->Packet_Queue->Size > 0){
                I2C->Busy_Flag = true;
                I2C->Current_Packet = (tI2C_Packet *)Dequeue(I2C->Packet_Queue);
                I2C->Busy_Flag = false;
            }
        } else {
            switch (I2C->Current_Packet->Op_type){
                I2C->Busy_Flag = true;
                case eI2C_Write:
                case eI2C_SingleRead:
                case eI2C_MemWrite:
                case eI2C_MemRead:
                static uint8_t I2C_Single_Attempts = 0;
                if (I2C_Single_Attempts < I2C->Current_Packet->Tries_timeout){
                    I2C_Single_Attempts++;
                    bool op_flag = false;
                    if (I2C->Current_Packet->Op_type == eI2C_Write){
                        if (HAL_I2C_Master_Transmit(I2C->I2C_Handle, I2C->Device_Address, I2C->Current_Packet->Data, I2C->Current_Packet->Data_Size, 1000) == HAL_OK){
                            op_flag = true;
                        }
                    }
                    else if (I2C->Current_Packet->Op_type == eI2C_SingleRead){
                        if (HAL_I2C_Master_Receive_DMA(I2C->I2C_Handle, I2C->Device_Address, I2C->Current_Packet->Data, I2C->Current_Packet->Data_Size) == HAL_OK){
                            op_flag = true;
                        }
                    } else if (I2C->Current_Packet->Op_type == eI2C_MemWrite){
                        if (HAL_I2C_Mem_Write(I2C->I2C_Handle, I2C->Device_Address, I2C->Current_Packet->Memory_Address, I2C->Current_Packet->Memory_Address_Size, I2C->Current_Packet->Data, I2C->Current_Packet->Data_Size, 1000) == HAL_OK){
                            op_flag = true;
                        }
                    } else if (I2C->Current_Packet->Op_type == eI2C_MemRead){
                        if (HAL_I2C_Mem_Read(I2C->I2C_Handle, I2C->Device_Address, I2C->Current_Packet->Memory_Address, I2C->Current_Packet->Memory_Address_Size, I2C->Current_Packet->Data, I2C->Current_Packet->Data_Size, 1000) == HAL_OK){
                            op_flag = true;
                        }
                    }

                    if (op_flag){
                        I2C_Single_Attempts = 0;
                        I2C->Busy_Flag = false;
                        I2C->Current_Packet->Success = true;
                        if (I2C->Current_Packet->Complete_CallBack != NULL){
                            I2C->Current_Packet->Complete_CallBack(I2C->Current_Packet->CallBack_Data);
                        }
                    }
                } else {
                    I2C_Single_Attempts = 0;
                    I2C->Busy_Flag = false;
                    I2C->Current_Packet->Success = false;
                    free(I2C->Current_Packet);
                    I2C->Current_Packet = NULL;
                }
                break;
            }
        }
    } else if (I2C->Mode == eMode_Continuous){
        return;
        // implement channel later if needed
    }
}