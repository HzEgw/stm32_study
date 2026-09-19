/**
 ******************************************************************************
 * @file    pwm.c
 * @brief   STM32F103C8 PWM 输出驱动 (标准外设库 StdPeriph)
 *
 *  为什么这么写(设计要点):
 *   1) 时钟不写死: 用 RCC_GetClocksFreq() + PPRE 位 实时算出"定时器计数时钟",
 *      以后改主频/改 APB 分频也不会算错。
 *   2) 占空比用千分比(0~1000) + 全整数运算: 0.1% 分辨率, 不引入软浮点。
 *   3) ARR/CCR 都开启预装载 + 配置完强制一次更新事件:
 *      运行中改频率或占空比, 新值在周期边界生效, 不会输出畸形波形。
 *   4) 死区时间以"纳秒"为入口, 内部按 RM0008 的四段公式换算 DTG,
 *      并把换算后的"实际死区"回读出来, 方便你用示波器核对。
 *   5) 全程注意 SWD(PA13/PA14) 不被占用, 保证 ST-Link 随时能下载/调试。
 ******************************************************************************
 */

#include "pwm.h"

/* ============================ 私有变量 ============================ */
static uint32_t s_tim3_clk   = 72000000u;      /* TIM3 计数时钟(Hz) */
static uint32_t s_tim3_period = 1000u;         /* TIM3 一个周期的计数个数 = ARR+1 */
static uint16_t s_duty[4]     = {0u, 0u, 0u, 0u};  /* 4 个通道当前占空比(千分比) */

static uint32_t s_tim1_clk    = 72000000u;     /* TIM1 计数时钟(Hz) */
static uint32_t s_tim1_period = 1000u;         /* TIM1 一个周期的计数个数 = ARR+1 */
static uint16_t s_adv_duty    = 0u;            /* TIM1 当前占空比(千分比) */
static uint32_t s_adv_dead_ns = 0u;            /* TIM1 实际死区时间(纳秒) */

/* ============================ 私有函数声明 ============================ */
static uint32_t PWM_GetTimerClock(TIM_TypeDef *TIMx);
static void     PWM_CalcPscArr(uint32_t freq_hz, uint32_t timer_clk,
                               uint16_t *psc, uint16_t *period);
static uint32_t PWM_DeadTimeToNs(uint8_t dtg, uint32_t timer_clk);
static uint8_t  PWM_DeadTimeFromNs(uint32_t ns, uint32_t timer_clk, uint32_t *actual_ns);

/* ============================ 时钟计算 ============================ */
/**
 * @brief  求某个定时器的计数时钟频率
 * @note   TIM1/TIM8 挂 APB2, TIM2~TIM7 挂 APB1;
 *         当 APB 预分频不为 1 时, 定时器时钟 = PCLKx * 2 (见参考手册时钟树)
 */
static uint32_t PWM_GetTimerClock(TIM_TypeDef *TIMx)
{
    RCC_ClocksTypeDef clocks;
    uint32_t ppre;

    RCC_GetClocksFreq(&clocks);

    if ((uint32_t)TIMx >= (uint32_t)APB2PERIPH_BASE)          /* TIM1 / TIM8 */
    {
        ppre = RCC->CFGR & RCC_CFGR_PPRE2;
        return (ppre == RCC_CFGR_PPRE2_DIV1) ? clocks.PCLK2_Frequency
                                             : (clocks.PCLK2_Frequency * 2u);
    }

    ppre = RCC->CFGR & RCC_CFGR_PPRE1;                        /* TIM2 ~ TIM7 */
    return (ppre == RCC_CFGR_PPRE1_DIV1) ? clocks.PCLK1_Frequency
                                         : (clocks.PCLK1_Frequency * 2u);
}

/**
 * @brief  根据目标频率计算 预分频(PSC) 与 周期计数(ARR+1)
 * @note   尽量保证每周期 >= 1000 个计数, 这样占空比才有 0.1% 的分辨率;
 *         频率很高时周期计数会不足 1000, 此时占空比分辨率相应降低(数学上必然)
 */
static void PWM_CalcPscArr(uint32_t freq_hz, uint32_t timer_clk,
                           uint16_t *psc, uint16_t *period)
{
    uint32_t div;      /* PSC + 1 */
    uint32_t cnt;      /* ARR + 1 */

    if (freq_hz == 0u)
    {
        freq_hz = 1u;
    }

    div = timer_clk / (freq_hz * PWM_DUTY_FULL);
    if (div == 0u)      { div = 1u; }
    if (div > 65536u)   { div = 65536u; }

    cnt = timer_clk / (freq_hz * div);
    if (cnt == 0u)      { cnt = 1u; }
    if (cnt > 65536u)   { cnt = 65536u; }

    *psc    = (uint16_t)(div - 1u);
    *period = (uint16_t)(cnt - 1u);      /* 这里回传的是 ARR 寄存器值 */
}

/* ============================ 死区换算 ============================ */
/**
 * @brief  DTG 编码值 -> 实际死区时间(纳秒), 按 RM0008 的四段公式反算
 */
static uint32_t PWM_DeadTimeToNs(uint8_t dtg, uint32_t timer_clk)
{
    uint32_t steps;      /* 死区 = steps 个 t_DTS */

    if (dtg <= 127u)      { steps = (uint32_t)dtg; }
    else if (dtg <= 191u) { steps = (64u + (uint32_t)(dtg & 0x3Fu)) * 2u; }
    else if (dtg <= 223u) { steps = (32u + (uint32_t)(dtg & 0x1Fu)) * 8u; }
    else                  { steps = (32u + (uint32_t)(dtg & 0x1Fu)) * 16u; }

    return (uint32_t)(((uint64_t)steps * 1000000000ULL) / (uint64_t)timer_clk);
}

/**
 * @brief  目标死区时间(纳秒) -> DTG 编码值, 并回传实际值
 * @note   t_DTS = 1 / timer_clk (CKD = 00 时)
 */
static uint8_t PWM_DeadTimeFromNs(uint32_t ns, uint32_t timer_clk, uint32_t *actual_ns)
{
    uint32_t steps = (uint32_t)(((uint64_t)ns * (uint64_t)timer_clk) / 1000000000ULL);
    uint8_t  dtg;

    if (steps > 1008u)    /* DTG 最大量程 = 63 * 16 = 1008 个 t_DTS */
    {
        steps = 1008u;
    }

    if (steps <= 127u)                                             /* 0xx: 1~127 */
    {
        dtg = (uint8_t)steps;
    }
    else if (steps <= 254u)                                        /* 10x: (64+0~63)*2 */
    {
        dtg = (uint8_t)(0x80u | (((steps + 1u) / 2u) - 64u));
    }
    else if (steps <= 504u)                                        /* 110: (32+0~31)*8 */
    {
        dtg = (uint8_t)(0xC0u | (((steps + 7u) / 8u) - 32u));
    }
    else                                                           /* 111: (32+0~31)*16 */
    {
        dtg = (uint8_t)(0xE0u | (((steps + 15u) / 16u) - 32u));
    }

    if (actual_ns != 0)
    {
        *actual_ns = PWM_DeadTimeToNs(dtg, timer_clk);
    }
    return dtg;
}

/* ============================ TIM3: 4 路 PWM ============================ */
/**
 * @brief  初始化 TIM3 的 4 路 PWM 输出 (PA6 / PA7 / PB0 / PB1)
 * @param  freq_hz  目标频率(Hz), 例如 1000 表示 1kHz
 * @note   4 个通道周期相同、占空比独立; 初始化后输出均为 0%, 由 PWM_SetDuty 设置
 */
void PWM_Init(uint32_t freq_hz)
{
    GPIO_InitTypeDef        GPIO_InitStructure;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_OCInitTypeDef       TIM_OCInitStructure;
    uint16_t psc;
    uint16_t arr;
    uint8_t  i;

    /* --- 时钟: GPIOA/GPIOB/AFIO 在 APB2, TIM3 在 APB1 --- */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
    /* ↑ 实际寄存器操作: RCC->APB2ENR |= (1<<0)|(1<<2)|(1<<3);   (AFIOEN / IOPAEN / IOPBEN) */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    /* ↑ 实际寄存器操作: RCC->APB1ENR |= (1<<1);                 (TIM3EN) */

    /* --- 引脚: 复用推挽输出(50MHz), TIM3 默认映射无需重映射 ---
     * 库函数最终写的是 GPIOA->CRL 与 GPIOB->CRL, 每 4 位管一个引脚:
     *     [3:2] CNF  = 10 (复用推挽输出)      [1:0] MODE = 11 (输出 50MHz)   合起来 = 0xB
     *     PA6 -> bit27:24   PA7 -> bit31:28   PB0 -> bit3:0   PB1 -> bit7:4
     */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_6 | GPIO_Pin_7;      /* CH1 / CH2 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    /* ↑ 等价于: GPIOA->CRL &= ~(0xFFu << 24);  GPIOA->CRL |= (0xBBu << 24); */

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_0 | GPIO_Pin_1;      /* CH3 / CH4 */
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    /* ↑ 等价于: GPIOB->CRL &= ~0xFFu;  GPIOB->CRL |= 0xBBu; */

    /* --- 时基: PSC / ARR 由目标频率算出 --- */
    s_tim3_clk = PWM_GetTimerClock(TIM3);
    PWM_CalcPscArr(freq_hz, s_tim3_clk, &psc, &arr);
    s_tim3_period = (uint32_t)arr + 1u;

    TIM_TimeBaseStructure.TIM_Prescaler         = psc;                /* -> TIM3->PSC */
    TIM_TimeBaseStructure.TIM_Period            = arr;                /* -> TIM3->ARR */
    TIM_TimeBaseStructure.TIM_ClockDivision     = TIM_CKD_DIV1;       /* -> TIM3->CR1 的 CKD[9:8] = 00 */
    TIM_TimeBaseStructure.TIM_CounterMode       = TIM_CounterMode_Up; /* -> TIM3->CR1 的 CMS[6:5] = 00(向上) */
    TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;                  /* 通用定时器无此功能, 给 0 */
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);
    /* ↑ 该函数内部: TIM3->PSC = psc; TIM3->ARR = arr; TIM3->CR1 配方向; 最后 TIM3->EGR |= (1<<0) 更新一次 */

    /* --- 4 个通道: PWM1 模式, 高电平有效, 初始 0% --- */
    TIM_OCInitStructure.TIM_OCMode       = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState  = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_OutputNState = TIM_OutputNState_Disable;  /* 通用定时器无互补 */
    TIM_OCInitStructure.TIM_Pulse        = 0;
    TIM_OCInitStructure.TIM_OCPolarity   = TIM_OCPolarity_High;
    TIM_OCInitStructure.TIM_OCNPolarity  = TIM_OCNPolarity_High;
    TIM_OCInitStructure.TIM_OCIdleState  = TIM_OCIdleState_Reset;
    TIM_OCInitStructure.TIM_OCNIdleState = TIM_OCNIdleState_Reset;

    TIM_OC1Init(TIM3, &TIM_OCInitStructure);   /* -> TIM3->CCMR1 的 OC1M[6:4]=110(PWM1) + TIM3->CCER 的 CC1E(bit0)=1 */
    TIM_OC2Init(TIM3, &TIM_OCInitStructure);   /* -> TIM3->CCMR1 的 OC2M[14:12]        + TIM3->CCER 的 CC2E(bit4)=1 */
    TIM_OC3Init(TIM3, &TIM_OCInitStructure);   /* -> TIM3->CCMR2 的 OC3M[6:4]          + TIM3->CCER 的 CC3E(bit8)=1 */
    TIM_OC4Init(TIM3, &TIM_OCInitStructure);   /* -> TIM3->CCMR2 的 OC4M[14:12]        + TIM3->CCER 的 CC4E(bit12)=1 */

    /* --- 预装载: ARR/CCR 写入影子寄存器, 周期边界生效 --- */
    TIM_OC1PreloadConfig(TIM3, TIM_OCPreload_Enable);   /* -> TIM3->CCMR1 |= (1u << 3);   OC1PE */
    TIM_OC2PreloadConfig(TIM3, TIM_OCPreload_Enable);   /* -> TIM3->CCMR1 |= (1u << 11);  OC2PE */
    TIM_OC3PreloadConfig(TIM3, TIM_OCPreload_Enable);   /* -> TIM3->CCMR2 |= (1u << 3);   OC3PE */
    TIM_OC4PreloadConfig(TIM3, TIM_OCPreload_Enable);   /* -> TIM3->CCMR2 |= (1u << 11);  OC4PE */
    TIM_ARRPreloadConfig(TIM3, ENABLE);                 /* -> TIM3->CR1   |= (1u << 7);   ARPE  */

    for (i = 0u; i < 4u; i++)
    {
        s_duty[i] = 0u;
    }

    TIM_Cmd(TIM3, ENABLE);                             /* -> TIM3->CR1 |= (1u << 0);  CEN 计数使能 */
    TIM_GenerateEvent(TIM3, TIM_EventSource_Update);   /* -> TIM3->EGR |= (1u << 0);  UG 立刻装入影子寄存器 */
}

/**
 * @brief  运行中修改频率, 4 个通道的占空比(千分比)保持不变
 * @note   PSC 立即生效 + ARR 预装载 -> 切换瞬间不会产生异常宽/窄的脉冲
 */
void PWM_SetFreq(uint32_t freq_hz)
{
    uint16_t psc;
    uint16_t arr;
    uint8_t  i;

    PWM_CalcPscArr(freq_hz, s_tim3_clk, &psc, &arr);

    TIM_PrescalerConfig(TIM3, psc, TIM_PSCReloadMode_Update);  /* -> TIM3->PSC = psc;   (立即生效) */
    TIM_SetAutoreload(TIM3, arr);                              /* -> TIM3->ARR = arr;   (有预装载, 周期边界生效) */
    s_tim3_period = (uint32_t)arr + 1u;
    TIM_GenerateEvent(TIM3, TIM_EventSource_Update);           /* -> TIM3->EGR |= (1u << 0); */

    for (i = 0u; i < 4u; i++)                 /* 按新周期重算 4 个 CCR */
    {
        PWM_SetDuty((uint8_t)(i + 1u), s_duty[i]);
    }
}

/**
 * @brief  设置某个通道的占空比
 * @param  ch   通道号 1~4
 * @param  duty 占空比千分比: 0 = 全低, 1000 = 全高(恒为高电平)
 */
void PWM_SetDuty(uint8_t ch, uint16_t duty)
{
    uint16_t ccr;

    if ((ch < 1u) || (ch > 4u))
    {
        return;
    }
    if (duty > PWM_DUTY_FULL)
    {
        duty = PWM_DUTY_FULL;
    }

    s_duty[ch - 1u] = duty;

    /* CCR = duty/1000 * 周期计数 (四舍五入); duty=1000 时 CCR = 周期计数 > ARR -> 恒高 */
    ccr = (uint16_t)(((uint32_t)duty * s_tim3_period + (PWM_DUTY_FULL / 2u)) / PWM_DUTY_FULL);

    switch (ch)
    {
        case 1u: TIM_SetCompare1(TIM3, ccr); break;   /* -> TIM3->CCR1 = ccr; */
        case 2u: TIM_SetCompare2(TIM3, ccr); break;   /* -> TIM3->CCR2 = ccr; */
        case 3u: TIM_SetCompare3(TIM3, ccr); break;   /* -> TIM3->CCR3 = ccr; */
        case 4u: TIM_SetCompare4(TIM3, ccr); break;   /* -> TIM3->CCR4 = ccr; */
        default: break;
    }
}

/**
 * @brief  读回通道当前占空比(千分比)
 */
uint16_t PWM_GetDuty(uint8_t ch)
{
    if ((ch < 1u) || (ch > 4u))
    {
        return 0u;
    }
    return s_duty[ch - 1u];
}

/**
 * @brief  读回 TIM3 实际频率(Hz), 可放 Keil Watch 窗口观察
 */
uint32_t PWM_GetActualFreq(void)
{
    uint32_t psc = TIM3->PSC;
    return s_tim3_clk / ((psc + 1u) * s_tim3_period);
}

/* ============================ TIM1: 互补输出 + 死区 ============================ */
/**
 * @brief  初始化 TIM1 互补 PWM (PA8 = CH1 主, PB13 = CH1N 互补) 并设置死区
 * @param  freq_hz      目标频率(Hz)
 * @param  deadtime_ns  死区时间(纳秒), 例如 1000 表示 1us
 * @note   死区用来避免"主/互补同时导通"造成桥臂直通短路, 驱动 H 桥时必须设置。
 *         高级定时器必须调用 TIM_CtrlPWMOutputs() 打开 MOE, 互补输出才会真正送出。
 */
void PWM_AdvInit(uint32_t freq_hz, uint32_t deadtime_ns)
{
    GPIO_InitTypeDef        GPIO_InitStructure;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_OCInitTypeDef       TIM_OCInitStructure;
    TIM_BDTRInitTypeDef     TIM_BDTRInitStructure;
    uint16_t psc;
    uint16_t arr;
    uint8_t  dtg;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_TIM1, ENABLE);
    /* ↑ 实际寄存器操作: RCC->APB2ENR |= (1<<2)|(1<<3)|(1<<11);   (IOPAEN / IOPBEN / TIM1EN) */

    /* --- 引脚: PA8 = CH1, PB13 = CH1N ---
     * 库函数写的是 GPIOA->CRH 与 GPIOB->CRH (CRH 管 8~15 号引脚, 每 4 位一个):
     *     PA8  -> CRH 的 bit3:0      PB13 -> CRH 的 bit23:20
     *     值 0xB = [CNF=10 复用推挽][MODE=11 50MHz]
     */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_8;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    /* ↑ 等价于: GPIOA->CRH &= ~0x0000000Fu;  GPIOA->CRH |= 0x0000000Bu; */

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_13;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    /* ↑ 等价于: GPIOB->CRH &= ~0x00F00000u;  GPIOB->CRH |= 0x00B00000u; */

    /* --- 时基 --- */
    s_tim1_clk = PWM_GetTimerClock(TIM1);
    PWM_CalcPscArr(freq_hz, s_tim1_clk, &psc, &arr);
    s_tim1_period = (uint32_t)arr + 1u;

    TIM_TimeBaseStructure.TIM_Prescaler         = psc;
    TIM_TimeBaseStructure.TIM_Period            = arr;
    TIM_TimeBaseStructure.TIM_ClockDivision     = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode       = TIM_CounterMode_Up;
    TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &TIM_TimeBaseStructure);   /* -> TIM1->PSC / TIM1->ARR / TIM1->CR1(CKD, CMS) */

    /* --- CH1 + CH1N 互补输出 --- */
    TIM_OCInitStructure.TIM_OCMode       = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState  = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_OutputNState = TIM_OutputNState_Enable;
    TIM_OCInitStructure.TIM_Pulse        = 0;
    TIM_OCInitStructure.TIM_OCPolarity   = TIM_OCPolarity_High;
    TIM_OCInitStructure.TIM_OCNPolarity  = TIM_OCNPolarity_High;
    TIM_OCInitStructure.TIM_OCIdleState  = TIM_OCIdleState_Reset;   /* 关断时主输出 = 低 */
    TIM_OCInitStructure.TIM_OCNIdleState = TIM_OCNIdleState_Reset;
    TIM_OC1Init(TIM1, &TIM_OCInitStructure);
    /* ↑ -> TIM1->CCMR1 的 OC1M[6:4]=110 (PWM1) ; TIM1->CCER 的 CC1E(bit0)=1 主输出
     *     外加 CC1NE(bit2)=1 互补输出使能、CC1P/CC1NP=0 高电平有效 --- 这是通用定时器没有的 */
    TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);   /* -> TIM1->CCMR1 |= (1u << 3);  OC1PE */
    TIM_ARRPreloadConfig(TIM1, ENABLE);                 /* -> TIM1->CR1   |= (1u << 7);  ARPE  */

    /* --- 死区 / 刹车 / 空闲电平 --- */
    dtg = PWM_DeadTimeFromNs(deadtime_ns, s_tim1_clk, &s_adv_dead_ns);

    TIM_BDTRInitStructure.TIM_OSSRState       = TIM_OSSRState_Enable;
    TIM_BDTRInitStructure.TIM_OSSIState       = TIM_OSSIState_Enable;
    TIM_BDTRInitStructure.TIM_LOCKLevel       = TIM_LOCKLevel_OFF;
    TIM_BDTRInitStructure.TIM_DeadTime        = dtg;
    TIM_BDTRInitStructure.TIM_Break           = TIM_Break_Disable;
    TIM_BDTRInitStructure.TIM_BreakPolarity   = TIM_BreakPolarity_High;
    TIM_BDTRInitStructure.TIM_AutomaticOutput = TIM_AutomaticOutput_Enable;
    TIM_BDTRConfig(TIM1, &TIM_BDTRInitStructure);
    /* ↑ 实际寄存器操作(一条搞定死区+刹车+主输出):
     *     TIM1->BDTR = DTG | (1u<<10) OSSI | (1u<<11) OSSR | (1u<<14) AOE | (1u<<15) MOE;
     *   注意 bit15 MOE 是"主输出使能", 高级定时器不置它, PA8/PB13 一点波形都没有 */

    TIM_CtrlPWMOutputs(TIM1, ENABLE);       /* -> TIM1->BDTR |= (1u << 15);  MOE = 1 */
    TIM_Cmd(TIM1, ENABLE);                  /* -> TIM1->CR1  |= (1u << 0);   CEN = 1 */
    TIM_GenerateEvent(TIM1, TIM_EventSource_Update);   /* -> TIM1->EGR |= (1u << 0);  UG 立刻装载 */
}

/**
 * @brief  设置 TIM1 互补对的占空比
 * @param  duty 0~1000 (主输出高电平占空比), 互补输出自动为 1 - duty
 */
void PWM_AdvSetDuty(uint16_t duty)
{
    uint16_t ccr;

    if (duty > PWM_DUTY_FULL)
    {
        duty = PWM_DUTY_FULL;
    }
    s_adv_duty = duty;

    ccr = (uint16_t)(((uint32_t)duty * s_tim1_period + (PWM_DUTY_FULL / 2u)) / PWM_DUTY_FULL);
    TIM_SetCompare1(TIM1, ccr);     /* -> TIM1->CCR1 = ccr;  (互补输出的占空比自动是 1 - duty) */
}

/**
 * @brief  读回实际死区时间(纳秒) —— 受 DTG 量化影响, 与设定值可能有偏差
 */
uint32_t PWM_AdvGetDeadTimeNs(void)
{
    return s_adv_dead_ns;
}

/**
 * @brief  使能/关断 TIM1 的互补输出(MOE 主输出使能)
 * @note   关断后 PA8/PB13 进入空闲电平(本工程为低), 可作为驱动 H 桥的"急停"
 */
void PWM_AdvEnable(FunctionalState state)
{
    TIM_CtrlPWMOutputs(TIM1, state);   /* -> TIM1->BDTR 的 MOE(bit15): 置 1 使能 / 清 0 关断 */
}
