/**
 ******************************************************************************
 * @file    main.c
 * @brief   PWM 输出实验 (TIM3 四路 + TIM1 互补带死区)
 *
 *  没有 LED、也还没学串口, 怎么验证? 三种办法:
 *   1) Keil 调试模式 → Watch 窗口: 看 g_xxx 这些全局变量;
 *   2) 万用表直流电压档 直接量 PA6/PA7/PB0/PB1:
 *        PWM 是方波, 万用表显示的是平均值 ≈ 3.3V × 占空比
 *        (50% ≈ 1.65V, 100% ≈ 3.3V, 0% ≈ 0V) —— 看通道2 的电压来回摆动就是呼吸效果;
 *   3) 示波器/逻辑分析仪: 双通道同时看 PA8(主) 与 PB13(互补), 能直接量出死区 1us。
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "pwm.h"

/* ============ 便于 Keil Watch 窗口观察的全局变量(volatile 防止被优化) ============ */
volatile uint16_t g_duty_ch1  = 0;      /* 通道1 占空比(千分比) */
volatile uint16_t g_duty_ch2  = 0;      /* 通道2 占空比(千分比) */
volatile uint16_t g_duty_ch3  = 0;      /* 通道3 占空比(千分比) */
volatile uint16_t g_duty_ch4  = 0;      /* 通道4 占空比(千分比) */
volatile uint32_t g_tim3_freq = 0;      /* TIM3 实际频率(Hz) */
volatile uint32_t g_tim1_freq = 0;      /* TIM1 实际频率(Hz) */
volatile uint32_t g_tim1_dead = 0;      /* TIM1 实际死区(ns) */
volatile uint32_t g_loop_cnt  = 0;      /* 主循环计数 */

/* ============================ 私有函数 ============================ */
static void Delay_ms(uint32_t ms);

/* ============================ 主函数 ============================ */
int main(void)
{
    int16_t duty = 0;
    int16_t step = 10;

    SystemInit();   /* 系统时钟 72MHz。它内部全是寄存器操作:
                     *   FLASH->ACR  = 0x12                       (2 个等待周期, 72MHz 必须)
                     *   RCC->CFGR  |= PLLSRC(HSE) | PLLMUL(×9) | PPRE1(/2) | PPRE2(/1)
                     *   RCC->CR    |= PLLON, 等 PLLRDY, 再把 SYSCLK 切到 PLL
                     * 结果: SYSCLK = HCLK = 72MHz, PCLK1 = 36MHz, PCLK2 = 72MHz */

    PWM_Init(PWM_DEF_FREQ_HZ);                          /* TIM3: PA6/PA7/PB0/PB1 */
    PWM_AdvInit(PWM_DEF_FREQ_HZ, PWM_ADV_DEADTIME_NS);  /* TIM1: PA8/PB13 + 1us 死区 */

    /* 固定 3 个通道的占空比, 方便用万用表逐个核对 */
    PWM_SetDuty(1, 500);                                /* 50%  ≈ 1.65V */
    PWM_SetDuty(3, 250);                                /* 25%  ≈ 0.83V */
    PWM_SetDuty(4, 750);                                /* 75%  ≈ 2.48V */

    g_tim3_freq = PWM_GetActualFreq();
    g_tim1_freq = PWM_GetActualFreq();                  /* 两个定时器同频, 便于对比 */
    g_tim1_dead = PWM_AdvGetDeadTimeNs();

    while (1)
    {
        /* 通道2 与 TIM1 互补对 一起"呼吸": 0% → 100% → 0%, 每步 10ms */
        duty += step;
        if (duty >= (int16_t)PWM_DUTY_FULL)
        {
            duty = (int16_t)PWM_DUTY_FULL;
            step = -10;
        }
        else if (duty <= 0)
        {
            duty = 0;
            step = 10;
        }

        PWM_SetDuty(2, (uint16_t)duty);
        PWM_AdvSetDuty((uint16_t)duty);

        /* 刷新观察变量 */
        g_duty_ch1 = PWM_GetDuty(1);
        g_duty_ch2 = PWM_GetDuty(2);
        g_duty_ch3 = PWM_GetDuty(3);
        g_duty_ch4 = PWM_GetDuty(4);
        g_loop_cnt++;

        Delay_ms(10);
    }
}

/* ============================ 简易毫秒延时 ============================ */
/**
 * @brief  轮询 SysTick 实现的毫秒延时(不开中断, 不影响 PWM 输出)
 * @note   这段本身就是寄存器操作, 顺便认识 Cortex-M3 内核的 SysTick 4 个寄存器:
 *           SysTick->LOAD   重装载值(计数器减到 0 后自动装回它)
 *           SysTick->VAL    当前计数值(写它可清零)
 *           SysTick->CTRL   控制位: bit0 ENABLE 使能 / bit1 TICKINT 中断(这里=0)
 *                                   bit2 CLKSOURCE(1 = 内核时钟 72MHz)
 *                                   bit16 COUNTFLAG(减到 0 置 1, **读一次自动清 0**)
 *           SysTick->CALIB  校准值(未使用)
 *         下面用到的 *_Msk 宏就是这些位的掩码, 等价写法: (1u<<0) / (1u<<2) / (1u<<16)
 */
static void Delay_ms(uint32_t ms)
{
    while (ms--)
    {
        SysTick->LOAD = (uint32_t)(SystemCoreClock / 1000u) - 1u;   /* 1ms 的计数值 */
        SysTick->VAL  = 0u;
        SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;

        while ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == 0u)
        {
            /* 等待计数到 0 (COUNTFLAG 置位) */
        }

        SysTick->CTRL = 0u;                                         /* 关掉计数 */
    }
}
