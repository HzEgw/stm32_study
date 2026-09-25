/**
 ******************************************************************************
 * @file    stm32f10x_conf.h
 * @brief   Standard Peripheral Library configuration file
 * @note    Enable only the peripherals used by this project (uncomment).
 *          Keeping unused modules commented reduces code size and build time.
 * @note    STM32F103C8 is a medium-density device. In Keil define:
 *              USE_STDPERIPH_DRIVER
 *              STM32F10X_MD
 ******************************************************************************
 */
#ifndef __STM32F10x_CONF_H
#define __STM32F10x_CONF_H

/* ===================== Includes ===================== */

/* Comment the line below to disable peripheral header file inclusion */
#include "stm32f10x_adc.h"
/* #include "stm32f10x_bkp.h" */
/* #include "stm32f10x_can.h" */
/* #include "stm32f10x_cec.h" */
/* #include "stm32f10x_crc.h" */
/* #include "stm32f10x_dac.h" */
/* #include "stm32f10x_dbgmcu.h" */
#include "stm32f10x_dma.h"
#include "stm32f10x_exti.h"
/* #include "stm32f10x_flash.h" */
/* #include "stm32f10x_fsmc.h" */
#include "stm32f10x_gpio.h"
#include "stm32f10x_i2c.h"   /* ★必开: 本工程用 I2C_Init / I2C_CheckEvent 等标准库函数 */
/* #include "stm32f10x_iwdg.h" */
#include "stm32f10x_pwr.h"
#include "stm32f10x_rcc.h"
/* #include "stm32f10x_rtc.h" */
/* #include "stm32f10x_sdio.h" */
#include "stm32f10x_spi.h"
#include "stm32f10x_tim.h"
#include "stm32f10x_usart.h"
/* #include "stm32f10x_wwdg.h" */
#include "misc.h"  /* High and medium density devices interrupt handler */

/* ===================== Assert configuration ===================== */
/* Uncomment the line below to enable full assert. */
/* #define USE_FULL_ASSERT    1 */

#ifdef  USE_FULL_ASSERT
  #define assert_param(expr) ((expr) ? (void)0 : assert_failed((uint8_t *)__FILE__, __LINE__))
  void assert_failed(uint8_t* file, uint32_t line);
#else
  #define assert_param(expr) ((void)0)
#endif /* USE_FULL_ASSERT */

#endif /* __STM32F10x_CONF_H */