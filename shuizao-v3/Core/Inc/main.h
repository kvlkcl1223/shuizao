/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define PG15_Pin GPIO_PIN_13
#define PG15_GPIO_Port GPIOC
#define PG16_Pin GPIO_PIN_14
#define PG16_GPIO_Port GPIOC
#define PG1_Pin GPIO_PIN_4
#define PG1_GPIO_Port GPIOA
#define PG2_Pin GPIO_PIN_5
#define PG2_GPIO_Port GPIOA
#define PG3_Pin GPIO_PIN_4
#define PG3_GPIO_Port GPIOC
#define PG4_Pin GPIO_PIN_5
#define PG4_GPIO_Port GPIOC
#define KEY1_Pin GPIO_PIN_2
#define KEY1_GPIO_Port GPIOB
#define PG5_Pin GPIO_PIN_12
#define PG5_GPIO_Port GPIOB
#define PG6_Pin GPIO_PIN_13
#define PG6_GPIO_Port GPIOB
#define PG7_Pin GPIO_PIN_14
#define PG7_GPIO_Port GPIOB
#define PG8_Pin GPIO_PIN_15
#define PG8_GPIO_Port GPIOB
#define PG9_Pin GPIO_PIN_6
#define PG9_GPIO_Port GPIOC
#define PG10_Pin GPIO_PIN_7
#define PG10_GPIO_Port GPIOC
#define PG11_Pin GPIO_PIN_8
#define PG11_GPIO_Port GPIOC
#define PG12_Pin GPIO_PIN_9
#define PG12_GPIO_Port GPIOC
#define KEY2_Pin GPIO_PIN_12
#define KEY2_GPIO_Port GPIOA
#define PG13_Pin GPIO_PIN_12
#define PG13_GPIO_Port GPIOC
#define PG14_Pin GPIO_PIN_2
#define PG14_GPIO_Port GPIOD
#define LED1_Pin GPIO_PIN_4
#define LED1_GPIO_Port GPIOB
#define LED2_Pin GPIO_PIN_5
#define LED2_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
