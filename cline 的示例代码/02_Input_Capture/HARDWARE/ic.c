/**
 ******************************************************************************
 * @file    ic.c
 * @brief   输入捕获驱动实现 (标准外设库) —— PWM 输入模式测频率 + 占空比
 *
 *  每条库函数后面都注明了它实际操作的寄存器, 方便对照寄存器版(02_Input_Capture_Reg)。
 ******************************************************************************
 */

#include "ic.h"

/* ============================ 私有变量 ============================ */
static volatile uint32_t s_period_us = 0;    /* 一个完整周期(us) */
static volatile uint32_t s_high_us   = 0;    /* 高电平时间(us)   */
static volatile uint32_t s_captures  = 0;    /* 捕获到的脉冲总数 */
static volatile uint32_t s_idle_ms   = 0;    /* 距上次捕获过去的毫秒数 */
static volatile uint8_t  s_lost      = 1u;   /* 1 = 无信号 */

static uint32_t s_freq_hz = 0;               /* 计算结果(Hz)    */
static uint16_t s_duty    = 0;               /* 计算结果(千分比) */

/* ============================ 初始化 ============================ */
/**
 * @brief  初始化 TIM3 为 PWM 输入模式 (PA6 / PA7), 并启动 SysTick 1ms 超时计时
 */
void IC_Init(void)
{
    GPIO_InitTypeDef        GPIO_InitStructure;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_ICInitTypeDef       TIM_ICInitStructure;
    NVIC_InitTypeDef        NVIC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);
    /* ↑ 实际寄存器操作: RCC->APB2ENR |= (1<<2)|(1<<0);   (IOPAEN / AFIOEN) */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    /* ↑ 实际寄存器操作: RCC->APB1ENR |= (1<<1);          (TIM3EN) */

    /* PA6/PA7: 浮空输入。TIM3 默认映射就是 PA6/PA7, 不需要重映射 */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    /* ↑ 等价于: GPIOA->CRL &= ~0xFF000000u;   (CNF=01 浮空输入, MODE=00 输入) */

    /* 时基: 1MHz 计数(1 计数 = 1us), ARR 拉满以扩大可测周期范围 */
    TIM_TimeBaseStructure.TIM_Prescaler         = IC_TIMER_PSC;      /* -> TIM3->PSC */
    TIM_TimeBaseStructure.TIM_Period            = 0xFFFF;            /* -> TIM3->ARR */
    TIM_TimeBaseStructure.TIM_ClockDivision     = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode       = TIM_CounterMode_Up;
    TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);

    /* ---- PWM 输入模式: 一次调用配好两个通道 ----
     * CH1: 直接映射 TI1 + 上升沿                     -> 测"周期"
     * CH2: (函数内部自动设成)间接映射 TI1 + 下降沿   -> 测"高电平时间"
     * 库函数实际写: TIM3->CCMR1 的 CC1S=01 / IC1F=滤波 / CC2S=10
     *               TIM3->CCER  的 CC1E=1, CC1P=0(上升沿), CC2E=1, CC2P=1(下降沿)
     */
    TIM_ICInitStructure.TIM_Channel     = TIM_Channel_1;
    TIM_ICInitStructure.TIM_ICPolarity  = TIM_ICPolarity_Rising;
    TIM_ICInitStructure.TIM_ICSelection = TIM_ICSelection_DirectTI;
    TIM_ICInitStructure.TIM_ICPrescaler = TIM_ICPSC_DIV1;    /* 每个边沿都捕获 */
    TIM_ICInitStructure.TIM_ICFilter    = IC_IC_FILTER;      /* 抗抖动滤波 */
    TIM_PWMIConfig(TIM3, &TIM_ICInitStructure);

    /* ---- 从模式"复位": 触发源 = TI1FP1 ----
     * 效果: 每个上升沿自动把 CNT 清零, 所以 CCR1 里存的就是"整个周期的计数"
     * 库函数实际写: TIM3->SMCR 的 TS[6:4]=101(TI1FP1), SMS[2:0]=100(复位), MSM=1
     */
    TIM_SelectInputTrigger(TIM3, TIM_TS_TI1FP1);
    TIM_SelectSlaveMode(TIM3, TIM_SlaveMode_Reset);
    TIM_SelectMasterSlaveMode(TIM3, TIM_MasterSlaveMode_Enable);

    /* ---- 捕获中断: CH1 用来算结果, CH2 用来刷新"有信号"计时 ---- */
    TIM_ITConfig(TIM3, TIM_IT_CC1 | TIM_IT_CC2, ENABLE);
    /* ↑ 实际寄存器操作: TIM3->DIER |= (1<<1)|(1<<2);   (CC1IE / CC2IE) */

    NVIC_InitStructure.NVIC_IRQChannel                   = TIM3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    TIM_Cmd(TIM3, ENABLE);     /* -> TIM3->CR1 |= (1<<0);  CEN 计数使能 */

    /* SysTick 1ms 中断: 只做一件事 —— 数"距离上次捕获过了多久" */
    SysTick_Config(SystemCoreClock / 1000u);
    NVIC_SetPriority(SysTick_IRQn, 3);
}

/**
 * @brief  可选: PB6 = TIM4_CH1 输出 1kHz / 30% 方波, 用跳线接到 PA6 即可自测
 * @note   不想自测就不调用它, 直接接外部信号源
 */
void IC_TestSignalInit(void)
{
    GPIO_InitTypeDef        GPIO_InitStructure;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_OCInitTypeDef       TIM_OCInitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);      /* -> RCC->APB2ENR |= (1<<3) */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);       /* -> RCC->APB1ENR |= (1<<2) */

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_6;                /* TIM4_CH1 = PB6 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    /* ↑ 等价于: GPIOB->CRL 的 bit27:24 = 0xB (复用推挽 50MHz) */

    /* 1MHz 计数: 周期 = (ARR+1) 个 us */
    TIM_TimeBaseStructure.TIM_Prescaler         = IC_TIMER_PSC;
    TIM_TimeBaseStructure.TIM_Period            = (1000000u / IC_TEST_FREQ_HZ) - 1u;
    TIM_TimeBaseStructure.TIM_ClockDivision     = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode       = TIM_CounterMode_Up;
    TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM4, &TIM_TimeBaseStructure);

    TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse       = ((1000000u / IC_TEST_FREQ_HZ) * IC_TEST_DUTY) / 1000u;
    TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_High;
    TIM_OC1Init(TIM4, &TIM_OCInitStructure);
    TIM_OC1PreloadConfig(TIM4, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM4, ENABLE);

    TIM_Cmd(TIM4, ENABLE);                                  /* -> TIM4->CR1 |= (1<<0) */
    TIM_GenerateEvent(TIM4, TIM_EventSource_Update);        /* -> TIM4->EGR |= (1<<0) */
}

/* ============================ 中断处理 ============================ */
/**
 * @brief  TIM3 捕获中断: CCR1 = 周期, CCR2 = 高电平时间
 * @note   由 stm32f10x_it.c 的 TIM3_IRQHandler 转发过来
 */
void IC_TIM3_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM3, TIM_IT_CC1) != RESET)      /* 读 TIM3->SR 的 CC1IF */
    {
        s_period_us = TIM_GetCapture1(TIM3);             /* 读 TIM3->CCR1 */
        s_high_us   = TIM_GetCapture2(TIM3);             /* 读 TIM3->CCR2 */

        if (s_period_us > 0u)
        {
            s_freq_hz = 1000000u / s_period_us;          /* 频率 = 1MHz / 周期计数(us) */
            s_duty    = (uint16_t)((s_high_us * 1000u) / s_period_us);   /* 千分比 */
        }

        s_idle_ms = 0u;
        s_lost    = 0u;
        s_captures++;

        TIM_ClearITPendingBit(TIM3, TIM_IT_CC1);         /* 写 TIM3->SR 的 CC1IF = 0 */
    }
    else if (TIM_GetITStatus(TIM3, TIM_IT_CC2) != RESET)
    {
        s_high_us = TIM_GetCapture2(TIM3);               /* 下降沿再刷一次高电平时间 */
        s_idle_ms = 0u;
        TIM_ClearITPendingBit(TIM3, TIM_IT_CC2);
    }
}

/**
 * @brief  SysTick 1ms 中断: 累加计时, 超时判定"无信号"
 * @note   中断里不做除法/打印, 只做计数 —— 好习惯
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
