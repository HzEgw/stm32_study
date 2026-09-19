/**
 ******************************************************************************
 * @file    main.c
 * @brief   编码器接口实验 —— 寄存器版
 *
 *  与 03_Encoder(标准库版) 现象完全一致。建议对照阅读:
 *      03_Encoder\HARDWARE\encoder.c   ←→   本工程 HARDWARE\encoder_reg.c
 *
 *  验证: 转编码器 -> g_position 增减 / g_rpm 变化 / PB6 万用表电压变化;
 *        没有编码器就用杜邦线快速碰 PA6 或 PA7 到 GND/3.3V。
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "encoder_reg.h"

/* ============ Keil Watch 观察点 ============ */
volatile int32_t  g_position = 0;
volatile int32_t  g_delta    = 0;
volatile int32_t  g_rpm      = 0;
volatile uint8_t  g_dir      = 0;
volatile uint16_t g_pwm_duty = 0;
volatile uint32_t g_samples  = 0;

/* 硬件寄存器真值 */
volatile uint32_t g_reg_cr1   = 0;   /* bit4 DIR 方向, bit0 CEN */
volatile uint32_t g_reg_smcr  = 0;   /* SMS[2:0]=011 编码器模式 3 */
volatile uint32_t g_reg_ccmr1 = 0;   /* 应为 0xF1F1 */
volatile uint32_t g_reg_cnt   = 0;   /* 位置计数, 随转动实时变化 */

int main(void)
{
    SystemInit();                 /* 72MHz */

    Encoder_SpeedOutputInit();    /* PB6 输出转速指示 PWM */
    Encoder_Init();               /* TIM3 编码器模式 + SysTick 节拍 */

    g_reg_smcr  = TIM3->SMCR;
    g_reg_ccmr1 = TIM3->CCMR1;

    while (1)
    {
        Encoder_Update();         /* 每 50ms 算一次 */

        g_position = Encoder_GetPosition();
        g_delta    = Encoder_GetDelta();
        g_rpm      = Encoder_GetRpm();
        g_dir      = Encoder_GetDirection();
        g_pwm_duty = Encoder_GetPwmDuty();
        g_samples  = Encoder_GetSamples();

        g_reg_cnt  = TIM3->CNT;
        g_reg_cr1  = TIM3->CR1;
    }
}
