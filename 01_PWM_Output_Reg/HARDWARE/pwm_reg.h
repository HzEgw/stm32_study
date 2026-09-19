/**
 ******************************************************************************
 * @file    pwm_reg.h
 * @brief   PWM 输出驱动 —— 纯寄存器版 (不依赖标准外设库)
 *
 *          功能与 01_PWM_Output(标准库版) 完全一致, 但所有配置都是
 *          "直接读写寄存器", 便于对照学习"库函数背后到底写了什么"。
 *
 *          引脚:
 *            TIM3 CH1~CH4 -> PA6 / PA7 / PB0 / PB1
 *            TIM1 CH1/CH1N -> PA8 / PB13 (互补 + 死区)
 *
 * ============================ 寄存器速查表 ============================
 *  寄存器            作用                     本文件用到的位
 *  ----------------  -----------------------  ------------------------------
 *  RCC->APB2ENR      APB2 外设时钟使能         bit0 AFIOEN, bit2 IOPAEN,
 *                                              bit3 IOPBEN, bit11 TIM1EN
 *  RCC->APB1ENR      APB1 外设时钟使能         bit1 TIM3EN
 *  RCC->CFGR         时钟配置                  bit10:8 PPRE1, bit13:11 PPRE2
 *  GPIOA->CRL/CRH    引脚配置(4 位/引脚)        MODE[1:0] CNF[1:0]
 *  TIM3->PSC         预分频                    16 位, 计数频率 = TIM3CLK/(PSC+1)
 *  TIM3->ARR         自动重装载(周期)          16 位, 频率 = 计数频率/(ARR+1)
 *  TIM3->CCMR1/2     通道模式                  OCxM[2:0]=110 PWM1, OCxPE=1 预装载
 *  TIM3->CCER        通道使能/极性             CCxE 使能, CCxP 极性
 *  TIM3->CCR1~CCR4   各通道比较值(=占空比)     高电平时间 = (CCR/周期) × 100%
 *  TIM3->CR1         控制寄存器                bit0 CEN 计数使能, bit7 ARPE 预装载
 *  TIM3->EGR         事件产生                  bit0 UG 更新事件(搬影子寄存器)
 *  TIM1->BDTR        刹车/死区/主输出          bit7:0 DTG 死区, bit10 OSSI,
 *                                              bit11 OSSR, bit14 AOE, bit15 MOE
 *  TIM1->CCER        通道使能/极性             CC1E bit0, CC1NE bit2 (互补)
 * =====================================================================
 ******************************************************************************
 */
#ifndef __PWM_REG_H
#define __PWM_REG_H

#include "stm32f10x.h"

/* ===================== 参数配置 ===================== */
#define PWM_DUTY_FULL        1000u    /* 占空比满量程(1000 = 100.0%) */
#define PWM_DEF_FREQ_HZ      1000u    /* 默认 PWM 频率(Hz) */
#define PWM_ADV_DEADTIME_NS  1000u    /* TIM1 互补对死区时间(纳秒) */

/* ===================== TIM3: 4 路 PWM ===================== */
void     PWM_Init(uint32_t freq_hz);
void     PWM_SetFreq(uint32_t freq_hz);
void     PWM_SetDuty(uint8_t ch, uint16_t duty);
uint16_t PWM_GetDuty(uint8_t ch);
uint32_t PWM_GetActualFreq(void);

/* ===================== TIM1: 互补输出 + 死区 ===================== */
void     PWM_AdvInit(uint32_t freq_hz, uint32_t deadtime_ns);
void     PWM_AdvSetDuty(uint16_t duty);
uint32_t PWM_AdvGetDeadTimeNs(void);
void     PWM_AdvEnable(FunctionalState state);

#endif /* __PWM_REG_H */
