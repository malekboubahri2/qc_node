/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2019 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
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
#include "stm32h7xx_hal.h"

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
#define LED3_Pin GPIO_PIN_11
#define LED3_GPIO_Port GPIOG
#define VSYNC_FREQ_Pin GPIO_PIN_14
#define VSYNC_FREQ_GPIO_Port GPIOH
#define RENDER_TIME_Pin GPIO_PIN_13
#define RENDER_TIME_GPIO_Port GPIOH
#define FRAME_RATE_Pin GPIO_PIN_9
#define FRAME_RATE_GPIO_Port GPIOI
#define GPIO0_Pin GPIO_PIN_6
#define GPIO0_GPIO_Port GPIOC
#define LED2_Pin GPIO_PIN_2
#define LED2_GPIO_Port GPIOG
#define GPIO_Pin GPIO_PIN_8
#define GPIO_GPIO_Port GPIOF
#define CH_PD_Pin GPIO_PIN_4
#define CH_PD_GPIO_Port GPIOA
#define TP_IRQ_Pin GPIO_PIN_2
#define TP_IRQ_GPIO_Port GPIOH
#define TP_IRQ_EXTI_IRQn EXTI2_IRQn
#define MCU_ACTIVE_Pin GPIO_PIN_9
#define MCU_ACTIVE_GPIO_Port GPIOH
#define LCD_BL_CTRL_Pin GPIO_PIN_1
#define LCD_BL_CTRL_GPIO_Port GPIOA
#define RST_Pin GPIO_PIN_8
#define RST_GPIO_Port GPIOH
#define LCD_ON_OFF_Pin GPIO_PIN_2
#define LCD_ON_OFF_GPIO_Port GPIOA

/* USER CODE BEGIN Private defines */
#define SDRAM_TIMEOUT                    ((uint32_t)0xFFFF)
#define REFRESH_COUNT                    ((uint32_t)0x0603)

#define SDRAM_MODEREG_BURST_LENGTH_1             ((uint16_t)0x0000)
#define SDRAM_MODEREG_BURST_LENGTH_2             ((uint16_t)0x0001)
#define SDRAM_MODEREG_BURST_LENGTH_4             ((uint16_t)0x0002)
#define SDRAM_MODEREG_BURST_LENGTH_8             ((uint16_t)0x0004)
#define SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL      ((uint16_t)0x0000)
#define SDRAM_MODEREG_BURST_TYPE_INTERLEAVED     ((uint16_t)0x0008)
#define SDRAM_MODEREG_CAS_LATENCY_2              ((uint16_t)0x0020)
#define SDRAM_MODEREG_CAS_LATENCY_3              ((uint16_t)0x0030)
#define SDRAM_MODEREG_OPERATING_MODE_STANDARD    ((uint16_t)0x0000)
#define SDRAM_MODEREG_WRITEBURST_MODE_PROGRAMMED ((uint16_t)0x0000)
#define SDRAM_MODEREG_WRITEBURST_MODE_SINGLE     ((uint16_t)0x0200)
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
