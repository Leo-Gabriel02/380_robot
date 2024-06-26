/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
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
#include "stm32g4xx_hal.h"

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

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LS_DIR_Pin GPIO_PIN_0
#define LS_DIR_GPIO_Port GPIOA
#define RS_DIR_Pin GPIO_PIN_1
#define RS_DIR_GPIO_Port GPIOA
#define USART2_TX_Pin GPIO_PIN_2
#define USART2_TX_GPIO_Port GPIOA
#define USART2_RX_Pin GPIO_PIN_3
#define USART2_RX_GPIO_Port GPIOA
#define SERVO_Pin GPIO_PIN_5
#define SERVO_GPIO_Port GPIOA
#define ENC_R_Pin GPIO_PIN_6
#define ENC_R_GPIO_Port GPIOA
#define ENC_L_Pin GPIO_PIN_7
#define ENC_L_GPIO_Port GPIOA
#define RS_M2_Pin GPIO_PIN_8
#define RS_M2_GPIO_Port GPIOA
#define RS_M1_Pin GPIO_PIN_9
#define RS_M1_GPIO_Port GPIOA
#define LS_M2_Pin GPIO_PIN_10
#define LS_M2_GPIO_Port GPIOA
#define LS_M1_Pin GPIO_PIN_11
#define LS_M1_GPIO_Port GPIOA
#define T_SWDIO_Pin GPIO_PIN_13
#define T_SWDIO_GPIO_Port GPIOA
#define T_SWCLK_Pin GPIO_PIN_14
#define T_SWCLK_GPIO_Port GPIOA
#define T_SWO_Pin GPIO_PIN_3
#define T_SWO_GPIO_Port GPIOB
#define LED_Pin GPIO_PIN_8
#define LED_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */
void runMotors(uint8_t side, uint8_t dir, double duty);
void setBit(uint32_t bitMask, uint8_t value);
void initSensors();
uint16_t readSensor(uint8_t sensor);
uint8_t selectMuxAddr(uint8_t sensor);
void calibrate(uint16_t* tape_val, uint16_t* wood_val);

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
