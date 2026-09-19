/**
 ******************************************************************************
 * @file    main.c
 * @brief   PWM 输出实验 —— 寄存器版
 *
 *  与标准库版(01_PWM_Output)现象完全一样, 只是驱动换成寄存器写法。
 *  建议对照阅读: 01_PWM_Output/HARDWARE/pwm.c  ←→  本工程 HARDWARE/pwm_reg.c
 *
 *  验证方法(不需要 LED / 串口):
 *   1) Keil Watch 窗口看 g_xxx 变量;
 *   2) 万用表直流电压档量 PA6/PA7/PB0/PB1: 电压 ≈ 3.3V × 占空比;
 *   3) 示波器双通道看 PA8 与 PB13 之间的死区。
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "pwm_reg.h"

/* ============ 便于 Watch 观察的全局变量 ============ */
volatile uint16_t g_duty_ch1  = 0;
volatile uint16_t g_duty_ch2  = 0;
volatile uint16_t g_duty_ch3  = 0;
volatile uint16_t g_duty_ch4  = 0;
volatile uint32_t g_tim3_freq = 0;
volatile uint32_t g_tim1_freq = 0;
volatile uint32_t g_tim1_dead = 0;
volatile uint32_t g_loop_cnt  = 0;

/* 直接读寄存器, 便于在 Watch 里"看硬件真实状态"(比变量更可信) */
volatile uint32_t g_reg_tim3_psc  = 0;
volatile uint32_t g_reg_tim3_arr  = 0;
volatile uint32_t g_reg_tim3_ccr1 = 0;
volatile uint32_t g_reg_tim3_ccmr1 = 0;
volatile uint32_t g_reg_tim3_ccer = 0;
volatile uint32_t g_reg_tim1_bdtr = 0;

static void Delay_ms(uint32_t ms);

int main(void)
{
    int16_t duty = 0;
    int16_t step = 10;

    /* SystemInit() 在 system_stm32f10x.c 里, 做两件寄存器级的事:
     *   FLASH->ACR  = 0x12    (2 个等待周期, 72MHz 必须)
     *   RCC->CFGR  |= PLLSRC(HSE) | PLLMUL(×9) | PPRE1(/2) | PPRE2(/1)
     *   RCC->CR    |= PLLON, 等待 PLLRDY, 再切 SYSCLK = PLL
     * 结果: SYSCLK = HCLK = 72MHz, PCLK1 = 36MHz, PCLK2 = 72MHz
     */
    SystemInit();

    PWM_Init(PWM_DEF_FREQ_HZ);                           /* TIM3 -> PA6/PA7/PB0/PB1 */
    PWM_AdvInit(PWM_DEF_FREQ_HZ, PWM_ADV_DEADTIME_NS);   /* TIM1 -> PA8/PB13 + 死区 */

    PWM_SetDuty(1, 500);        /* 50% */
    PWM_SetDuty(3, 250);        /* 25% */
    PWM_SetDuty(4, 750);        /* 75% */

    g_tim3_freq = PWM_GetActualFreq();
    g_tim1_freq = PWM_GetActualFreq();
    g_tim1_dead = PWM_AdvGetDeadTimeNs();

    /* 直接把寄存器快照读出来看 */
    g_reg_tim3_psc   = TIM3->PSC;
    g_reg_tim3_arr   = TIM3->ARR;
    g_reg_tim3_ccr1  = TIM3->CCR1;
    g_reg_tim3_ccmr1 = TIM3->CCMR1;
    g_reg_tim3_ccer  = TIM3->CCER;
    g_reg_tim1_bdtr  = TIM1->BDTR;

    while (1)
    {
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

        g_duty_ch1 = PWM_GetDuty(1);
        g_duty_ch2 = PWM_GetDuty(2);
        g_duty_ch3 = PWM_GetDuty(3);
        g_duty_ch4 = PWM_GetDuty(4);
        g_reg_tim3_ccr1 = TIM3->CCR1;      /* 看 CCR 真的在变 */
        g_loop_cnt++;

        Delay_ms(10);
    }
}

/**
 * @brief  毫秒延时 —— 本来就是寄存器操作, 顺便当例子看
 *
 *  SysTick 是 Cortex-M3 内核里的 24 位递减计数器, 4 个寄存器:
 *     SysTick->LOAD  重装载值(计到 0 后自动装回它)
 *     SysTick->VAL   当前计数值(写它可清零)
 *     SysTick->CTRL  控制: bit0 ENABLE 使能, bit1 TICKINT 中断, bit2 CLKSOURCE(1=内核时钟),
 *                    bit16 COUNTFLAG 计数到 0 时置 1, **读一次自动清零**
 *     SysTick->CALIB 校准值(不用)
 *  这里让 CLKSOURCE = 1(72MHz), LOAD = 72000-1 -> 每 1ms 到一次,
 *  用轮询 COUNTFLAG 判断, 不开中断。
 */
static void Delay_ms(uint32_t ms)
{
    while (ms--)
    {
        SysTick->LOAD = (uint32_t)(SystemCoreClock / 1000u) - 1u;
        SysTick->VAL  = 0u;
        SysTick->CTRL = (1u << 2) | (1u << 0);   /* CLKSOURCE=1, ENABLE=1, TICKINT=0 */

        while ((SysTick->CTRL & (1u << 16)) == 0u)   /* 等 COUNTFLAG */
        {
        }

        SysTick->CTRL = 0u;                      /* ENABLE=0 */
    }
}
