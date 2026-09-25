/**
 ******************************************************************************
 * @file    pwm.h
 * @brief   STM32F103C8 PWM 输出驱动 (标准外设库 StdPeriph)
 *
 *          TIM3 (通用定时器, APB1): 4 路独立 PWM
 *              CH1 -> PA6    CH2 -> PA7    CH3 -> PB0    CH4 -> PB1
 *          TIM1 (高级定时器, APB2): 1 对互补 PWM + 死区
 *              CH1 -> PA8    CH1N -> PB13
 *
 *          占空比统一用 0~1000 的"千分比"表示(分辨率 0.1%),
 *          全整数运算, 不引入浮点( Cortex-M3 无 FPU )。
 *
 *          注意: 所有引脚均避开 SWD(PA13/PA14) 与串口(PA9/PA10),
 *                用 ST-Link 下载/调试不受影响。
 *
 * ============ 寄存器速查(想深入看寄存器写法请打开同工作空间的 01_PWM_Output_Reg) ============
 *   RCC->APB2ENR   bit0 AFIOEN, bit2 IOPAEN, bit3 IOPBEN, bit11 TIM1EN
 *   RCC->APB1ENR   bit1 TIM3EN
 *   GPIOx->CRL/CRH 每 4 位一个引脚: [3:2] CNF  [1:0] MODE   (复用推挽 50MHz = 0xB)
 *   TIMx->PSC/ARR  计数频率 = TIMCLK/(PSC+1);  PWM 频率 = 计数频率/(ARR+1)
 *   TIMx->CCMR1/2  OCxM[2:0]=110 为 PWM 模式1, OCxPE=1 为 CCR 预装载
 *   TIMx->CCER     CCxE 输出使能(bit0/4/8/12); TIM1 另有 CC1NE(bit2) 互补使能
 *   TIMx->CCR1~4   比较值 = 占空比
 *   TIMx->CR1      bit0 CEN 启动, bit7 ARPE ARR 预装载
 *   TIMx->EGR      bit0 UG 立即产生更新事件
 *   TIM1->BDTR     bit7:0 DTG 死区, bit10 OSSI, bit11 OSSR, bit14 AOE, bit15 MOE 主输出使能
 * ==========================================================================================
 */
#ifndef __PWM_H
#define __PWM_H

#include "stm32f10x.h"

/* ===================== 参数配置 ===================== */
#define PWM_DUTY_FULL        1000u    /* 占空比满量程(1000 = 100.0%) */
#define PWM_DEF_FREQ_HZ      1000u    /* 默认 PWM 频率(Hz) */
#define PWM_ADV_DEADTIME_NS  1000u    /* TIM1 互补对死区时间(纳秒) */

/* ===================== TIM3: 4 路 PWM ===================== */
void     PWM_Init(uint32_t freq_hz);                 /* 初始化, freq_hz = 目标频率 */
void     PWM_SetFreq(uint32_t freq_hz);              /* 运行中改频率, 占空比保持不变 */
void     PWM_SetDuty(uint8_t ch, uint16_t duty);     /* ch: 1~4, duty: 0~1000 */
uint16_t PWM_GetDuty(uint8_t ch);                    /* 读回当前占空比(千分比) */
uint32_t PWM_GetActualFreq(void);                    /* 读回实际频率(Hz), 可放 Watch 观察 */

/* ===================== TIM1: 互补输出 + 死区 ===================== */
void     PWM_AdvInit(uint32_t freq_hz, uint32_t deadtime_ns);
void     PWM_AdvSetDuty(uint16_t duty);              /* duty: 0~1000 */
uint32_t PWM_AdvGetDeadTimeNs(void);                 /* 实际死区时间(纳秒) */
void     PWM_AdvEnable(FunctionalState state);       /* 关断/使能 互补输出(MOE) */

#endif /* __PWM_H */
