/**
 ******************************************************************************
 * @file    PWM.h
 * @brief   TIM2 三路 PWM —— 负责板上 3 个 LED 的"亮度"
 *
 *          引脚：PA1 = TIM2_CH2   PA2 = TIM2_CH3   PA3 = TIM2_CH4
 *                （TIM2 默认复用，无需重映射；CH1 = PA0 留给按键，互不干扰）
 *          频率：1 kHz（PSC=71 / ARR=999，推导过程见 PWM.c 顶部）
 *          占空比：0~1000 的"千分比"，与 CCR 寄存器一一对应，不用换算
 *
 *          为什么用 PWM 而不是"GPIO 快速开关"？
 *            亮度 = 方波的平均电压 = 3.3V × 占空比。
 *            若用 GPIO + 延时去模拟，CPU 全耗在延时里，什么都干不了；
 *            交给硬件定时器，只是往 CCR 写个数，CPU 空出来扫按键。
 ******************************************************************************
 */
#ifndef __PWM_H
#define __PWM_H

#include "stm32f10x.h"

/* ===================== 参数 ===================== */
#define PWM_FULL        1000u   /* 占空比满量程：1000 = 100.0% */
#define PWM_CH_LED1     2u      /* TIM2_CH2 -> PA1 */
#define PWM_CH_LED2     3u      /* TIM2_CH3 -> PA2 */
#define PWM_CH_LED3     4u      /* TIM2_CH4 -> PA3 */

/* ===================== 接口 ===================== */
void     PWM_Init(void);                                /* 初始化 TIM2 三路 PWM（含引脚复用） */
void     PWM_SetCompare(uint8_t ch, uint16_t duty);     /* ch: 2/3/4, duty: 0~1000 */
uint16_t PWM_GetCompare(uint8_t ch);                    /* 读回 CCR（Watch 观察用） */
uint32_t PWM_GetFreqHz(void);                           /* 实际 PWM 频率，应为 1000 */

#endif /* __PWM_H */
