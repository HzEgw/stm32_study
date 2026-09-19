/**
 ******************************************************************************
 * @file    stm32f10x_conf.h
 * @brief   Standard Peripheral Library configuration file
 * @note    Only the peripherals used by this project need to be enabled.
 *          本工程实际用到: GPIO / RCC / TIM / USART / ADC / I2C / misc
 *          (其余留着注释也无妨, 保持一致便于工程间复用)
 * @note    In Keil define:
 *              USE_STDPERIPH_DRIVER
 *              STM32F10X_MD
 ******************************************************************************
 */
#ifndef __STM32F10x_CONF_H
#define __STM32F10x_CONF_H

/* ===================== Includes ===================== */

/* Comment the line below to disable peripheral header file inclusion */
#include "stm32f10x_adc.h"      /* 电池电压 */
/* #include "stm32f10x_bkp.h" */
/* #include "stm32f10x_can.h" */
/* #include "stm32f10x_cec.h" */
/* #include "stm32f10x_crc.h" */
/* #include "stm32f10x_dac.h" */
/* #include "stm32f10x_dbgmcu.h" */
/* #include "stm32f10x_dma.h" */
/* #include "stm32f10x_exti.h" */
/* #include "stm32f10x_flash.h" */
/* #include "stm32f10x_fsmc.h" */
#include "stm32f10x_gpio.h"     /* LED/按键/方向脚/PWM 脚 */
#include "stm32f10x_i2c.h"      /* I2C2 扫描 IMU */
/* #include "stm32f10x_iwdg.h" */
/* #include "stm32f10x_pwr.h" */
#include "stm32f10x_rcc.h"      /* 时钟 */
/* #include "stm32f10x_rtc.h" */
/* #include "stm32f10x_sdio.h" */
/* #include "stm32f10x_spi.h" */
#include "stm32f10x_tim.h"      /* PWM + 编码器接口 */
#include "stm32f10x_usart.h"    /* 串口打印 */
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