/**
 ******************************************************************************
 * @file    main.c
 * @brief   输入捕获实验 —— 寄存器版 (PWM 输入模式测频率 + 占空比)
 *
 *  与 02_Input_Capture(标准库版) 现象完全一样, 建议左右分屏对照:
 *      02_Input_Capture\HARDWARE\ic.c   ←→   本工程 HARDWARE\ic_reg.c
 *
 *  自测: 一根跳线 PB6 → PA6, 然后看 Watch 窗口:
 *      g_freq_hz = 1000 , g_period = 1000 , g_high = 300 , g_duty = 300 , g_lost = 0
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "ic_reg.h"

/* ============ Keil Watch 观察点 ============ */
volatile uint32_t g_freq_hz  = 0;
volatile uint16_t g_period   = 0;
volatile uint16_t g_high     = 0;
volatile uint16_t g_duty     = 0;
volatile uint32_t g_captures = 0;
volatile uint8_t  g_lost     = 0;

/* 直接看寄存器的真实值, 比看变量更能说明问题 */
volatile uint32_t g_reg_ccmr1 = 0;   /* TIM3->CCMR1 : CC1S/IC1F/CC2S/IC2F */
volatile uint32_t g_reg_ccer  = 0;   /* TIM3->CCER  : CC1E/CC1P/CC2E/CC2P */
volatile uint32_t g_reg_smcr  = 0;   /* TIM3->SMCR  : SMS/TS/MSM        */
volatile uint32_t g_reg_ccr1  = 0;   /* TIM3->CCR1  : 周期计数(us)      */
volatile uint32_t g_reg_ccr2  = 0;   /* TIM3->CCR2  : 高电平计数(us)    */
volatile uint32_t g_reg_cnt   = 0;   /* TIM3->CNT   : 当前计数值(一直在跑) */

int main(void)
{
    SystemInit();            /* 72MHz */

    IC_TestSignalInit();     /* PB6 输出 1kHz/30% 方波: RCC->APB2ENR/APB1ENR, GPIOB->CRL, TIM4->* */
    IC_Init();               /* PA6/PA7 捕获: GPIOA->CRL, TIM3->CCMR1/CCER/SMCR/DIER, SysTick */

    g_reg_ccmr1 = TIM3->CCMR1;   /* 一次性快照, 便于对照注释理解每一位 */
    g_reg_ccer  = TIM3->CCER;
    g_reg_smcr  = TIM3->SMCR;

    while (1)
    {
        g_freq_hz  = IC_GetFreqHz();
        g_period   = IC_GetPeriodUs();
        g_high     = IC_GetHighUs();
        g_duty     = IC_GetDutyPermille();
        g_captures = IC_GetCaptureCount();
        g_lost     = IC_IsSignalLost();

        g_reg_ccr1 = TIM3->CCR1;     /* 周期: 预期 1000 */
        g_reg_ccr2 = TIM3->CCR2;     /* 高电平: 预期 300 */
        g_reg_cnt  = TIM3->CNT;      /* 在 0~1000 之间循环 */
    }
}
