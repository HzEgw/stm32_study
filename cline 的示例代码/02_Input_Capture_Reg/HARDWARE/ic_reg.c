/**
 ******************************************************************************
 * @file    ic_reg.c
 * @brief   输入捕获驱动 —— 纯寄存器实现
 *
 *  配置顺序(和标准库版一一对应):
 *    ① 开时钟  ② 配 PA6/PA7 为浮空输入  ③ 时基 PSC/ARR
 *    ④ CCMR1 配两个通道的映射与滤波      ⑤ CCER 配边沿
 *    ⑥ SMCR 配从模式"复位"               ⑦ DIER 开中断
 *    ⑧ NVIC (CMSIS 内核函数)             ⑨ CR1 启动 + SysTick 1ms
 ******************************************************************************
 */

#include "ic_reg.h"

/* ============================ 私有变量 ============================ */
static volatile uint32_t s_period_us = 0;
static volatile uint32_t s_high_us   = 0;
static volatile uint32_t s_captures  = 0;
static volatile uint32_t s_idle_ms   = 0;
static volatile uint8_t  s_lost      = 1u;

static uint32_t s_freq_hz = 0;
static uint16_t s_duty    = 0;

/* ============================ 初始化 ============================ */
void IC_Init(void)
{
    /* ---------- ① 时钟 ---------- */
    RCC->APB2ENR |= (1u << 0)      /* AFIOEN */
                  | (1u << 2);     /* IOPAEN */
    RCC->APB1ENR |= (1u << 1);     /* TIM3EN */

    /* ---------- ② PA6/PA7 = 浮空输入 ----------
     * GPIOA->CRL 每 4 位一个引脚: [3:2] CNF=01(浮空输入)  [1:0] MODE=00(输入)  -> 0x4
     *   PA6 -> bit27:24     PA7 -> bit31:28
     */
    GPIOA->CRL &= ~0xFF000000u;    /* 先清零 */
    GPIOA->CRL |=  0x44000000u;    /* PA6 = 0x4, PA7 = 0x4 */

    /* ---------- ③ 时基: 1MHz 计数(1 计数 = 1us), ARR 拉满 ---------- */
    TIM3->PSC = IC_TIMER_PSC;      /* 预分频 */
    TIM3->ARR = 0xFFFFu;           /* 自动重装 */
    TIM3->CR1 &= ~(1u << 4);       /* DIR(bit4) = 0 向上计数 */

    /* ---------- ④ CCMR1: 通道映射 + 滤波 ----------
     *  CC1S[1:0] = 01  IC1 直接用 TI1(PA6)
     *  IC1PSC    = 00  每个边沿都捕获
     *  IC1F      = 0011 滤波: 连续 4 次采样一致才认边沿 (抗抖动)
     *  CC2S[1:0] = 10  IC2 间接映射到 TI1 (还是 PA6 这根线, 但取下降沿!)
     *  IC2F      = 0011 同样滤波
     */
    TIM3->CCMR1 = (uint16_t)((0x01u << 0)     /* CC1S  */
                           | (0x00u << 2)     /* IC1PSC*/
                           | ((IC_IC_FILTER & 0x0Fu) << 4)    /* IC1F */
                           | (0x02u << 8)     /* CC2S  */
                           | (0x00u << 10)    /* IC2PSC*/
                           | ((IC_IC_FILTER & 0x0Fu) << 12)); /* IC2F */

    /* ---------- ⑤ CCER: 使能 + 边沿 ----------
     *  CC1E(bit0)=1 使能, CC1P(bit1)=0 上升沿
     *  CC2E(bit4)=1 使能, CC2P(bit5)=1 下降沿
     */
    TIM3->CCER = (1u << 0) | (1u << 4) | (1u << 5);

    /* ---------- ⑥ SMCR: 从模式复位 ----------
     *  SMS[2:0] = 100  复位模式: 每次触发把 CNT 清零
     *  TS[6:4]  = 101  触发源 = TI1FP1(CH1 上升沿)
     *  MSM(bit7)= 1    主/从模式使能
     *  结果: CNT 从 0 数到下一个上升沿 -> CCR1 就是"一个周期的计数"
     */
    TIM3->SMCR = (uint16_t)((0x04u << 0)
                          | (0x05u << 4)
                          | (1u << 7));

    /* ---------- ⑦ 中断: CH1 算结果, CH2 刷新"有信号"计时 ---------- */
    TIM3->DIER |= (1u << 1) | (1u << 2);    /* CC1IE / CC2IE */

    /* ---------- ⑧ NVIC: 这里是 CMSIS 内核函数(不需要 misc.c) ----------
     *  NVIC_EnableIRQ(TIM3_IRQn) 等价于 NVIC->ISER[0] |= (1u << (TIM3_IRQn & 0x1F));
     *  NVIC_SetPriority(x, p)   等价于写 NVIC->IPR[x] (优先级位数 __NVIC_PRIO_BITS = 4)
     */
    NVIC_SetPriority(TIM3_IRQn, 1);
    NVIC_EnableIRQ(TIM3_IRQn);

    /* ---------- ⑨ 启动 + SysTick 1ms(这次要开中断) ---------- */
    TIM3->CR1 |= (1u << 0);                    /* CEN = 1 计数使能 */

    SysTick->LOAD = (SystemCoreClock / 1000u) - 1u;
    SysTick->VAL  = 0u;
    SysTick->CTRL = (1u << 2)     /* CLKSOURCE = 1 (内核时钟 72MHz) */
                  | (1u << 1)     /* TICKINT   = 1 (计数到 0 产生中断) */
                  | (1u << 0);    /* ENABLE    = 1 */
    NVIC_SetPriority(SysTick_IRQn, 3);
}

/**
 * @brief  可选: PB6(TIM4_CH1) 输出 1kHz/30% 方波, 跳线到 PA6 即可自测
 */
void IC_TestSignalInit(void)
{
    uint16_t arr = (uint16_t)((1000000u / IC_TEST_FREQ_HZ) - 1u);

    RCC->APB2ENR |= (1u << 3);     /* IOPBEN */
    RCC->APB1ENR |= (1u << 2);     /* TIM4EN */

    /* PB6 = 复用推挽输出 50MHz : GPIOB->CRL 的 bit27:24 = 0xB */
    GPIOB->CRL &= ~0x0F000000u;
    GPIOB->CRL |=  0x0B000000u;

    TIM4->PSC = IC_TIMER_PSC;      /* 1MHz 计数 */
    TIM4->ARR = arr;               /* 周期 = (ARR+1) us */

    /* CCMR1: OC1M[6:4] = 110 (PWM 模式1), OC1PE(bit3) = 1 (预装载) */
    TIM4->CCMR1 = (uint16_t)((0x06u << 4) | (1u << 3));
    /* CCER: CC1E(bit0) = 1 使能输出, CC1P(bit1) = 0 高有效 */
    TIM4->CCER  = (1u << 0);
    /* 比较值 = 周期计数 × 占空比 */
    TIM4->CCR1  = (uint16_t)(((uint32_t)(arr + 1u) * IC_TEST_DUTY) / 1000u);

    TIM4->CR1 |= (1u << 7);        /* ARPE 预装载 */
    TIM4->CR1 |= (1u << 0);        /* CEN  启动 */
    TIM4->EGR |= (1u << 0);        /* UG   立刻装载 */
}

/* ============================ 中断处理 ============================ */
/**
 * @brief  TIM3 捕获中断(寄存器版)
 * @note   SR 的标志位是"写 0 清除"(rc_w0), 所以清标志要写 ~(1<<n)
 */
void IC_TIM3_IRQHandler(void)
{
    uint16_t sr = TIM3->SR;

    if ((sr & (1u << 1)) != 0u)                 /* CC1IF: 上升沿捕获完成 */
    {
        s_period_us = TIM3->CCR1;               /* 一个周期的计数(us) */
        s_high_us   = TIM3->CCR2;               /* 高电平时间(us) */

        if (s_period_us > 0u)
        {
            s_freq_hz = 1000000u / s_period_us;
            s_duty    = (uint16_t)((s_high_us * 1000u) / s_period_us);
        }

        s_idle_ms = 0u;
        s_lost    = 0u;
        s_captures++;

        TIM3->SR = (uint16_t)~(1u << 1);        /* 只清 CC1IF, 其他位写 1 不受影响 */
    }
    else if ((sr & (1u << 2)) != 0u)            /* CC2IF: 下降沿捕获完成 */
    {
        s_high_us = TIM3->CCR2;
        s_idle_ms = 0u;
        TIM3->SR = (uint16_t)~(1u << 2);
    }
}

/**
 * @brief  SysTick 1ms 中断: 超时判定
 * @note   SysTick 的 COUNTFLAG 读一次自动清零, 这里不需要手动清标志
 */
void IC_SysTickHandler(void)
{
    s_idle_ms++;
    if (s_idle_ms > IC_TIMEOUT_MS)
    {
        s_lost    = 1u;
        s_freq_hz = 0u;
        s_duty    = 0u;
    }
}

/* ============================ 读结果 ============================ */
uint32_t IC_GetFreqHz(void)        { return s_freq_hz; }
uint16_t IC_GetDutyPermille(void)  { return s_duty; }
uint16_t IC_GetPeriodUs(void)      { return (uint16_t)s_period_us; }
uint16_t IC_GetHighUs(void)        { return (uint16_t)s_high_us; }
uint32_t IC_GetCaptureCount(void)  { return s_captures; }
uint8_t  IC_IsSignalLost(void)     { return s_lost; }
