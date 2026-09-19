/**
 ******************************************************************************
 * @file    ic_reg.h
 * @brief   输入捕获驱动 —— 纯寄存器版 (PWM 输入模式测频率 + 占空比)
 *
 *  功能与 02_Input_Capture(标准库版) 完全一致, 只是全部直接写寄存器。
 *
 * ============================ 寄存器速查表 ============================
 *  寄存器        作用                    本文件用到的位
 *  ------------  ----------------------  ------------------------------------
 *  RCC->APB2ENR  时钟使能                bit0 AFIOEN, bit2 IOPAEN, bit3 IOPBEN
 *  RCC->APB1ENR  时钟使能                bit1 TIM3EN, bit2 TIM4EN
 *  GPIOA->CRL    引脚 0~7                PA6=bit27:24, PA7=bit31:28
 *                                        CNF=01 浮空输入 -> 0x4
 *  TIM3->PSC/ARR 时基                    1MHz 计数, ARR=0xFFFF
 *  TIM3->CCMR1   通道 1/2 模式           CC1S[1:0]=01 直连TI1, IC1F[7:4] 滤波
 *                                        CC2S[9:8]=10 间接TI1,  IC2F[15:12] 滤波
 *  TIM3->CCER    使能/极性               CC1E(bit0) CC1P(bit1) CC2E(bit4) CC2P(bit5)
 *  TIM3->SMCR    从模式                  SMS[2:0]=100 复位, TS[6:4]=101 TI1FP1, MSM(bit7)
 *  TIM3->DIER    中断使能                CC1IE(bit1) CC2IE(bit2)
 *  TIM3->SR      状态标志                CC1IF(bit1) CC2IF(bit2)  (写 0 清除)
 *  TIM3->CCR1/2  捕获结果                周期 / 高电平时间
 *  SysTick->*    1ms 计时                LOAD/VAL/CTRL(bit0 ENABLE bit1 TICKINT bit2 CLKSOURCE)
 * =====================================================================
 ******************************************************************************
 */
#ifndef __IC_REG_H
#define __IC_REG_H

#include "stm32f10x.h"

/* ===================== 参数配置 ===================== */
#define IC_TIMER_PSC      71u        /* 72MHz / (71+1) = 1MHz, 1 计数 = 1us */
#define IC_IC_FILTER      0x03u      /* 输入滤波: 连续 4 次采样一致才认边沿 */
#define IC_TIMEOUT_MS     100u       /* 100ms 没捕获 -> 认为无信号 */
#define IC_TEST_FREQ_HZ   1000u      /* 自测信号频率(Hz) */
#define IC_TEST_DUTY      300u       /* 自测信号占空比(千分比) */

/* ===================== 对外接口 ===================== */
void     IC_Init(void);
void     IC_TestSignalInit(void);
uint32_t IC_GetFreqHz(void);
uint16_t IC_GetDutyPermille(void);
uint16_t IC_GetPeriodUs(void);
uint16_t IC_GetHighUs(void);
uint32_t IC_GetCaptureCount(void);
uint8_t  IC_IsSignalLost(void);

void     IC_TIM3_IRQHandler(void);
void     IC_SysTickHandler(void);

#endif /* __IC_REG_H */
