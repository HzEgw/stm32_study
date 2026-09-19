/**
 ******************************************************************************
 * @file    ic.h
 * @brief   输入捕获驱动 —— PWM 输入模式, 一次测出"频率 + 占空比"
 *
 *  原理(初学者必读):
 *    TIM3_CH1 (PA6) 捕获 **上升沿** -> 得到"周期"
 *    TIM3_CH2 (PA7) 捕获 **下降沿** -> 得到"高电平时间"
 *    再把定时器配成"从模式-复位": 每次 CH1 上升沿自动把 CNT 清零,
 *    于是 CNT 从 0 数到本次上升沿的计数 = 一个完整周期。
 *
 *  分辨率: 72MHz / (PSC+1) = 1MHz -> 1 个计数 = 1us
 *  量程  : ARR = 65535 -> 最长可测周期 65535us -> 最低约 15Hz;
 *          最高频率受中断开销限制, 本工程实测到 100kHz 量级没问题。
 *
 *  引脚:
 *    PA6 = TIM3_CH1 (输入, 上升沿)
 *    PA7 = TIM3_CH2 (输入, 下降沿)
 *    PB6 = TIM4_CH1 (输出, 自测方波 1kHz/30%) —— 一根跳线 PB6->PA6 即可自测
 ******************************************************************************
 */
#ifndef __IC_H
#define __IC_H

#include "stm32f10x.h"

/* ===================== 参数配置 ===================== */
#define IC_TIMER_PSC      71u        /* 72MHz / (71+1) = 1MHz, 1 计数 = 1us */
#define IC_IC_FILTER      0x03u      /* 输入滤波: 连续 4 次采样一致才认边沿 */
#define IC_TIMEOUT_MS     100u       /* 100ms 没捕获到 -> 认为无信号(0Hz) */
#define IC_TEST_FREQ_HZ   1000u      /* 自测信号频率(Hz) */
#define IC_TEST_DUTY      300u       /* 自测信号占空比(千分比) */

/* ===================== 对外接口 ===================== */
void     IC_Init(void);                      /* TIM3 PWM 输入模式 + SysTick 1ms 超时 */
void     IC_TestSignalInit(void);            /* PB6 输出自测方波(可选) */
uint32_t IC_GetFreqHz(void);                 /* 频率 Hz */
uint16_t IC_GetDutyPermille(void);           /* 占空比 千分比 0~1000 */
uint16_t IC_GetPeriodUs(void);               /* 周期 us */
uint16_t IC_GetHighUs(void);                 /* 高电平时间 us */
uint32_t IC_GetCaptureCount(void);           /* 累计捕获次数(判断有没有信号) */
uint8_t  IC_IsSignalLost(void);              /* 1 = 已超时/无信号 */

/* 供 stm32f10x_it.c 里的中断服务函数调用 */
void     IC_TIM3_IRQHandler(void);           /* TIM3 捕获中断 */
void     IC_SysTickHandler(void);            /* SysTick 1ms 中断(超时计时) */

#endif /* __IC_H */
