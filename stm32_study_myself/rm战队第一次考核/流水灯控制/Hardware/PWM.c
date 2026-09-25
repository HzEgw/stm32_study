/**
 ******************************************************************************
 * @file    PWM.c
 * @brief   TIM2 三路 PWM（1 kHz，千分比占空比）
 *
 *  ============ 为什么是这几个数（考核被问就照这个讲） ============
 *   1) TIM2 挂在 APB1 上。APB1 预分频 = 2 时 PCLK1 = 36MHz；
 *      但 F1 的时钟树规定：APB 分频 ≠ 1 时，定时器时钟 = PCLK1 × 2
 *      → TIM2 计数时钟 = 36MHz × 2 = **72MHz**
 *   2) PSC = 71   → 计数频率 = 72MHz / (71+1) = **1MHz**（每个计数 = 1µs）
 *   3) ARR = 999  → 一个 PWM 周期 = 1000 个计数 = 1000µs
 *      → PWM 频率 = 1 / 1000µs = **1kHz**（正好 1000Hz，不零不整）
 *   4) 于是"占空比"与 CCR 一一对应：CCR=250 → 25% → LED 呈现 25% 亮度
 *      （好处：示波器 / 万用表 / Watch 三处看到的数都对得上）
 *   5) 三个通道共用同一个 TIM2 → 频率天生一致，只有占空比各自独立，
 *      这正是"呼吸灯 / 波浪灯"需要的：同频、不同亮度、随时可改。
 *
 *  ============ 引脚（TIM2 默认复用，无重映射） ============
 *      CH1 = PA0  ← 不用（PA0 留给按键）
 *      CH2 = PA1  → LED1
 *      CH3 = PA2  → LED2
 *      CH4 = PA3  → LED3
 ******************************************************************************
 */

#include "PWM.h"

#define PWM_PSC     71u                 /* 72MHz / (71+1) = 1MHz */
#define PWM_ARR     (PWM_FULL - 1u)     /* 999 → 一个周期 1000 个计数 */

/**
 * @brief  初始化 TIM2 的 3 路 PWM 输出（PA1 / PA2 / PA3）
 * @note   可重复调用：换效果时再进 PWM 模式会重新配一遍，不会出问题
 */
void PWM_Init(void)
{
    GPIO_InitTypeDef        GPIO_InitStructure;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_OCInitTypeDef       TIM_OCInitStructure;

    /* --- 1. 开时钟：GPIOA 在 APB2，TIM2 在 APB1 --- */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2,  ENABLE);
    /* ↑ 实际寄存器：RCC->APB2ENR |= (1<<2);   RCC->APB1ENR |= (1<<0);
     *   不开时钟，后面所有寄存器写入都无效（新手最常踩的第一个坑） */

    /* --- 2. 引脚：PA1/PA2/PA3 配成"复用推挽输出"，交给 TIM2 驱动 --- */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;        /* 复用推挽 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    /* ↑ 实际寄存器：GPIOA->CRL 每 4 位一个引脚，0xB = 复用推挽 50MHz
     *   PA1 -> bit7:4   PA2 -> bit11:8   PA3 -> bit15:12   → 0xBBB0 */

    /* --- 3. 时基：PSC / ARR 决定频率，也决定"占空比满量程" --- */
    TIM_TimeBaseStructInit(&TIM_TimeBaseStructure);
    TIM_TimeBaseStructure.TIM_Prescaler     = PWM_PSC;              /* -> TIM2->PSC */
    TIM_TimeBaseStructure.TIM_Period        = PWM_ARR;              /* -> TIM2->ARR */
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;         /* -> CR1 的 CKD = 00 */
    TIM_TimeBaseStructure.TIM_CounterMode   = TIM_CounterMode_Up;   /* -> CR1 的 CMS = 00（向上计数） */
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

    TIM_ClearFlag(TIM2, TIM_FLAG_Update);           /* 清掉初始化时可能产生的更新标志 */
    TIM_ARRPreloadConfig(TIM2, ENABLE);             /* -> TIM2->CR1 |= (1<<7);  ARR 预装载 */

    /* --- 4. 三个输出通道：PWM 模式 1 + 高电平有效 + CCR 初值 0 --- */
    TIM_OCStructInit(&TIM_OCInitStructure);
    TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;          /* CNT < CCR 时输出有效电平 */
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;   /* 通道输出使能 CCxE = 1 */
    TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_High;      /* 有效电平 = 高 → CCR 越大越亮 */
    TIM_OCInitStructure.TIM_Pulse       = 0;                        /* 初始 0% → 上电先灭着 */
    TIM_OC2Init(TIM2, &TIM_OCInitStructure);    /* -> CCMR1 的 OC2M=110 / OC2PE=1；CCER 的 CC2E=1 */
    TIM_OC3Init(TIM2, &TIM_OCInitStructure);
    TIM_OC4Init(TIM2, &TIM_OCInitStructure);

    /* --- 5. 启动定时器 --- */
    TIM_Cmd(TIM2, ENABLE);      /* -> TIM2->CR1 |= (1<<0);  CEN = 1 */
}

/**
 * @brief  设置某一通道的占空比
 * @param  ch   PWM_CH_LED1 / 2 / 3（= 2 / 3 / 4，对应 PA1 / PA2 / PA3）
 * @param  duty 0~1000，1000 = 100%
 * @note   有上限保护：写超过满量程没有意义（100% 已经是最亮）
 */
void PWM_SetCompare(uint8_t ch, uint16_t duty)
{
    if (duty > PWM_FULL)
    {
        duty = PWM_FULL;
    }

    switch (ch)
    {
        case PWM_CH_LED1: TIM_SetCompare2(TIM2, duty); break;   /* -> TIM2->CCR2 = duty */
        case PWM_CH_LED2: TIM_SetCompare3(TIM2, duty); break;   /* -> TIM2->CCR3 = duty */
        case PWM_CH_LED3: TIM_SetCompare4(TIM2, duty); break;   /* -> TIM2->CCR4 = duty */
        default: break;                                        /* 非法通道：什么都不做 */
    }
}

/**
 * @brief  读回某通道当前占空比（0~1000）
 */
uint16_t PWM_GetCompare(uint8_t ch)
{
    switch (ch)
    {
        case PWM_CH_LED1: return (uint16_t)TIM2->CCR2;
        case PWM_CH_LED2: return (uint16_t)TIM2->CCR3;
        case PWM_CH_LED3: return (uint16_t)TIM2->CCR4;
        default:          return 0u;
    }
}

/**
 * @brief  读回实际 PWM 频率(Hz)
 * @note   f = 定时器时钟 / ((PSC+1) × (ARR+1)) = 72MHz / (72 × 1000) = 1000
 */
uint32_t PWM_GetFreqHz(void)
{
    return SystemCoreClock / (((uint32_t)PWM_PSC + 1u) * ((uint32_t)PWM_ARR + 1u));
}
