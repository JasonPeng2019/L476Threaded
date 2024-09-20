################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (9-2020-q2-update)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Middlewares/Scheduler/scheduler.c 

OBJS += \
./Core/Middlewares/Scheduler/scheduler.o 

C_DEPS += \
./Core/Middlewares/Scheduler/scheduler.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Middlewares/Scheduler/%.o: ../Core/Middlewares/Scheduler/%.c Core/Middlewares/Scheduler/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32L476xx -c -I../Core/Inc -I../Drivers/STM32L4xx_HAL_Driver/Inc -I../Drivers/STM32L4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32L4xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Middlewares-2f-Scheduler

clean-Core-2f-Middlewares-2f-Scheduler:
	-$(RM) ./Core/Middlewares/Scheduler/scheduler.d ./Core/Middlewares/Scheduler/scheduler.o

.PHONY: clean-Core-2f-Middlewares-2f-Scheduler

