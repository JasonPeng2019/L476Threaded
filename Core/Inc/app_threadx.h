/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_threadx.h
  * @author  MCD Application Team
  * @brief   ThreadX applicative header file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2020-2021 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __APP_THREADX_H__
#define __APP_THREADX_H__

#ifdef __cplusplus
extern "C" {
#endif
/* Includes ------------------------------------------------------------------*/
#include "tx_api.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Private defines -----------------------------------------------------------*/
/* USER CODE BEGIN PD */

/*--------------------------------------------THREADS--------------------------------------------*/
#define TX_APP_THREAD_STACK_SIZE                2048
#define TX_SMALL_APP_THREAD_STACK_SIZE          1024
/*--------------------------------------------THREADS--------------------------------------------*/





/*--------------------------------------------MUTEX--------------------------------------------*/

/*--------------------------------------------MUTEX--------------------------------------------*/



/*--------------------------------------------SEMAPHORES--------------------------------------------*/

/*--------------------------------------------SEMAPHORES--------------------------------------------*/


/*--------------------------------------------EVENT FLAGS GROUP--------------------------------------------*/

/*--------------------------------------------EVENT FLAGS GROUP--------------------------------------------*/


/*--------------------------------------------QUEUES--------------------------------------------*/
/*--------------------------------------------QUEUES--------------------------------------------*/

/*--------------------------------------------PIPES--------------------------------------------*/
/*--------------------------------------------PIPES--------------------------------------------*/


/*--------------------------------------------MEMORY MANAGEMENT--------------------------------------------*/
#define TX_APP_MEM_POOL_SIZE                    16384
#define TX_APP_BLOCK_SIZE                       64
#define TX_APP_BLOCK_COUNT                      128

/*--------------------------------------------MEMORY MANAGEMENT--------------------------------------------*/


/*--------------------------------------------SOFTWARE TIMERS--------------------------------------------*/
/*--------------------------------------------SOFTWARE TIMERS--------------------------------------------*/



/* USER CODE END PD */

/* Main thread defines -------------------------------------------------------*/
/* USER CODE BEGIN MTD */

/* USER CODE END MTD */

/* Exported macro ------------------------------------------------------------*/

/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
UINT App_ThreadX_Init(VOID *memory_ptr);
void MX_ThreadX_Init(void);
/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

#ifdef __cplusplus
}
#endif
#endif /* __APP_THREADX_H__ */
