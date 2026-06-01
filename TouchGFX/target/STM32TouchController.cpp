/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * File Name          : STM32TouchController.cpp
 ******************************************************************************
 * This file was created by TouchGFX Generator 4.25.0. This file is only
 * generated once! Delete this file from your project and re-generate code
 * using STM32CubeMX or change this file manually to update it.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2025 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */

/* USER CODE BEGIN STM32TouchController */
#include <touchgfx/hal/HAL.hpp>
#include <touchgfx/hal/Types.hpp>
#include <STM32TouchController.hpp>
#include "main.h"

volatile bool doSampleTouch = false;

extern "C" I2C_HandleTypeDef hi2c4;

static uint8_t ts_address = 0;
static uint8_t ts_reg_bytes = 0;
static uint8_t ts_status_mask = 0;
static uint8_t ts_touch_msb_mask = 0;
static uint16_t ts_pos_reg = 0;
static uint16_t ts_status_reg = 0;
static uint8_t ts_x_low_pos = 0;
static uint8_t ts_x_high_pos = 0;
static uint8_t ts_y_low_pos = 0;
static uint8_t ts_y_high_pos = 0;


using namespace touchgfx;
extern "C"
{
    void HAL_GPIO_EXTI_Callback(uint16_t GPIO__Pin)
    {
        if (GPIO__Pin == TP_IRQ_Pin && HAL_GPIO_ReadPin(TP_IRQ_GPIO_Port, TP_IRQ_Pin) == GPIO_PIN_RESET)
        {
            /* Communication with TS is done via I2C.
            Often the sw requires ISRs (interrupt service routines) to be quick while communication
            with I2C can be considered relatively long (depending on SW requirements).
            Considering that the TS feature don't need immediate reaction,
            it is suggested to use polling mode instead of EXTI mode,
            in order to avoid blocking I2C communication on interrupt service routines */

            /* Here an example of implementation is proposed which is a mix between pooling and exit mode:
            On ISR a flag is set (exti5_received), the main loop polls on the flag rather then polling the TS;
            Mcu communicates with TS only when the flag has been set by ISR. This is just an example:
            the users should choose they strategy depending on their application needs.*/

            doSampleTouch = true;
            return;
        }
    }
}
void STM32TouchController::init()
{
    //wait to make sure touch controller is ready
    HAL_Delay(50);

    uint8_t id = 0;
    HAL_I2C_Mem_Read(&hi2c4, GT911_I2C_ADDR, GT911_ID_REG, 2, &id, 1, HAL_MAX_DELAY);
    if (id == GT911_ID)
    {
        ts_reg_bytes = GT911_REG_BYTES;
        ts_address = GT911_I2C_ADDR;
        ts_pos_reg = GT911_TOUCH_POS_REG;
        ts_status_reg = GT911_STATUS_REG;
        ts_status_mask = GT911_STATUS_MASK;
        ts_touch_msb_mask = GT911_TOUCH_MSB_MASK;
        ts_x_low_pos = 0;
        ts_x_high_pos = 1;
        ts_y_low_pos = 2;
        ts_y_high_pos = 3;
    }
    else
    {
        ts_reg_bytes = FT5336_REG_BYTES;
        ts_address = FT5336_I2C_ADDR;
        ts_pos_reg = FT5336_TOUCH_POS_REG;
        ts_status_reg = FT5336_STATUS_REG;
        ts_status_mask = FT5336_STATUS_MASK;
        ts_touch_msb_mask = FT5336_TOUCH_MSB_MASK;
        ts_x_low_pos = 3;
        ts_x_high_pos = 2;
        ts_y_low_pos = 1;
        ts_y_high_pos = 0;
    }
}

bool STM32TouchController::sampleTouch(int32_t& x, int32_t& y)
{
    uint8_t touches = 0;
    uint8_t buf[6];
    uint8_t ZERO = 0;

    NVIC_DisableIRQ(EXTI2_IRQn);
    if (doSampleTouch)
    {
        HAL_I2C_Mem_Read(&hi2c4, ts_address, ts_status_reg, ts_reg_bytes, buf, 1, HAL_MAX_DELAY);
        touches = (ts_status_mask & buf[0]);

        // Clear the status register, only for GT911
        if (ts_address == GT911_I2C_ADDR)
        {
            HAL_I2C_Mem_Write(&hi2c4, ts_address, ts_status_reg, ts_reg_bytes, &ZERO, 1, HAL_MAX_DELAY);
        }

        doSampleTouch = false;

        if (touches > 0)
        {
            HAL_I2C_Mem_Read(&hi2c4, ts_address, ts_pos_reg, ts_reg_bytes, buf, 4, HAL_MAX_DELAY);
            x = buf[ts_x_low_pos] + ((buf[ts_x_high_pos] & ts_touch_msb_mask) << 8);
            y = buf[ts_y_low_pos] + ((buf[ts_y_high_pos] & ts_touch_msb_mask) << 8);
        }
    }
    NVIC_EnableIRQ(EXTI2_IRQn);

    return (touches > 0);
}
/* USER CODE END STM32TouchController */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
