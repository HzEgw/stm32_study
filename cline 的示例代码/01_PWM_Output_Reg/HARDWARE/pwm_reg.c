/**
 ******************************************************************************
 * @file    pwm_reg.c
 * @brief   PWM 输出驱动 —— 纯寄存器版 (不用任何标准库函数)
 *
 *  阅读方法(给初学者):
 *    每一处配置都写成  <寄存器> = <值>  的形式, 并在注释里说明:
 *      1) 为什么先开时钟(RCC->APBxENR)?  —— 外设不供时钟, 写寄存器无效;
 *      2) 这 4 位/这几位分别是什么意思(CNF/MODE/OCxM/CEN/MOE ...);
 *      3) 库函数版里对应的写法是什么(见 01_PWM_Output/HARDWARE/pwm.c)。
 ******************************************************************************
 */

#include "pwm_reg.h"

/* ============================ 私有变量 ============================ */
static uint32_t s_clk      = 72000000u;   /* TIM3 计数时钟(Hz) */
static uint32_t s_period   = 1000u;       /* TIM3 周期计数个数 = ARR + 1 */
static uint16_t s_duty[4]  = {0u, 0u, 0u, 0u};

static uint32_t s_adv_clk     = 72000000u;/* TIM1 计数时钟(Hz) */
static uint32_t s_adv_period  = 1000u;
static uint16_t s_adv_duty    = 0u;
static uint32_t s_adv_dead_ns = 0u;

/* ============================ 私有函数 ============================ */
static uint32_t PWM_GetTimerClock(TIM_TypeDef *TIMx);
static void     PWM_CalcPscArr(uint32_t freq_hz, uint32_t timer_clk,
                               uint16_t *psc, uint16_t *arr);
static uint32_t PWM_DeadTimeToNs(uint8_t dtg, uint32_t timer_clk);
static uint8_t  PWM_DeadTimeFromNs(uint32_t ns, uint32_t timer_clk, uint32_t *actual_ns);

/* ============================ 时钟计算(读 RCC->CFGR) ============================ */
/**
 * @brief  求定时器计数时钟(Hz)
 *
 *  RCC->CFGR 里:
 *      bit10:8  PPRE1[2:0]  APB1 分频 (TIM2~TIM7 属于 APB1)
 *      bit13:11 PPRE2[2:0]  APB2 分频 (TIM1/TIM8 属于 APB2)
 *      编码: 0xx = 不分频, 100 = /2, 101 = /4, 110 = /8, 111 = /16
 *  规则: APB 分频系数 ≠ 1 时, 定时器时钟 = PCLK × 2 (参考手册"时钟树")
 *       —— 本工程 72MHz 系统下: APB1=/2 -> 36MHz, 但 TIM2~TIM7 仍是 72MHz
 */
static uint32_t PWM_GetTimerClock(TIM_TypeDef *TIMx)
{
    uint32_t hclk = SystemCoreClock;
    uint32_t ppre;
    uint32_t pclk;

    if ((uint32_t)TIMx >= (uint32_t)APB2PERIPH_BASE)      /* TIM1 / TIM8 */
    {
        ppre = (RCC->CFGR >> 11) & 0x07u;                 /* 取 PPRE2[2:0] */
    }
    else                                                  /* TIM2 ~ TIM7 */
    {
        ppre = (RCC->CFGR >> 8) & 0x07u;                  /* 取 PPRE1[2:0] */
    }

    pclk = (ppre < 0x04u) ? hclk : (hclk >> (ppre - 3u)); /* 算 PCLK1/PCLK2 */

    return (ppre < 0x04u) ? pclk : (pclk * 2u);           /* 定时器时钟 */
}

/**
 * @brief  目标频率 -> PSC / ARR 寄存器值
 *  PSC: 计数频率 = 定时器时钟 / (PSC + 1)
 *  ARR: PWM 频率 = 计数频率 / (ARR + 1)
 *  尽量让 (ARR+1) >= 1000, 这样占空比才有 0.1% 的分辨率
 */
static void PWM_CalcPscArr(uint32_t freq_hz, uint32_t timer_clk,
                           uint16_t *psc, uint16_t *arr)
{
    uint32_t div;      /* = PSC + 1 */
    uint32_t cnt;      /* = ARR + 1 */

    if (freq_hz == 0u)
    {
        freq_hz = 1u;
    }

    div = timer_clk / (freq_hz * PWM_DUTY_FULL);
    if (div == 0u)    { div = 1u; }
    if (div > 65536u) { div = 65536u; }

    cnt = timer_clk / (freq_hz * div);
    if (cnt == 0u)    { cnt = 1u; }
    if (cnt > 65536u) { cnt = 65536u; }

    *psc = (uint16_t)(div - 1u);      /* 写入 TIMx->PSC 的值 */
    *arr = (uint16_t)(cnt - 1u);      /* 写入 TIMx->ARR 的值 */
}

/* ============================ 死区换算(DTG 位域) ============================ */
/**
 * @brief  TIM1->BDTR 的 DTG[7:0] -> 实际死区时间(纳秒)
 *  RM0008 公式(CKD=00 时 t_DTS = 1 个定时器时钟):
 *      DTG = 0xxxxxxx : 死区 = DTG            × t_DTS
 *      DTG = 10xxxxxx : 死区 = (64 + 低6位) × 2  × t_DTS
 *      DTG = 110xxxxx : 死区 = (32 + 低5位) × 8  × t_DTS
 *      DTG = 111xxxxx : 死区 = (32 + 低5位) × 16 × t_DTS
 */
static uint32_t PWM_DeadTimeToNs(uint8_t dtg, uint32_t timer_clk)
{
    uint32_t steps;

    if (dtg <= 127u)      { steps = (uint32_t)dtg; }
    else if (dtg <= 191u) { steps = (64u + (uint32_t)(dtg & 0x3Fu)) * 2u; }
    else if (dtg <= 223u) { steps = (32u + (uint32_t)(dtg & 0x1Fu)) * 8u; }
    else                  { steps = (32u + (uint32_t)(dtg & 0x1Fu)) * 16u; }

    return (uint32_t)(((uint64_t)steps * 1000000000ULL) / (uint64_t)timer_clk);
}

/**
 * @brief  目标死区(纳秒) -> DTG[7:0], 并回传实际死区
 */
static uint8_t PWM_DeadTimeFromNs(uint32_t ns, uint32_t timer_clk, uint32_t *actual_ns)
{
    uint32_t steps = (uint32_t)(((uint64_t)ns * (uint64_t)timer_clk) / 1000000000ULL);
    uint8_t  dtg;

    if (steps > 1008u)     /* DTG 最大 63 × 16 = 1008 个 t_DTS */
    {
        steps = 1008u;
    }

    if (steps <= 127u)
    {
        dtg = (uint8_t)steps;                                    /* 0xxxxxxx */
    }
    else if (steps <= 254u)
    {
        dtg = (uint8_t)(0x80u | (((steps + 1u) / 2u) - 64u));    /* 10xxxxxx */
    }
    else if (steps <= 504u)
    {
        dtg = (uint8_t)(0xC0u | (((steps + 7u) / 8u) - 32u));    /* 110xxxxx */
    }
    else
    {
        dtg = (uint8_t)(0xE0u | (((steps + 15u) / 16u) - 32u));  /* 111xxxxx */
    }

    if (actual_ns != 0)
    {
        *actual_ns = PWM_DeadTimeToNs(dtg, timer_clk);
    }
    return dtg;
}

/* ============================ TIM3: 4 路 PWM (纯寄存器) ============================ */
/**
 * @brief  初始化 TIM3 CH1~CH4 -> PA6 / PA7 / PB0 / PB1
 *
 *  整个过程就 6 步, 每一步只碰 1~2 个寄存器:
 *    ① 开时钟 ② 配引脚 ③ 配时基(PSC/ARR) ④ 配通道模式(CCMR)
 *    ⑤ 使能输出(CCER) ⑥ 预装载 + 启动(CR1/EGR)
 */
void PWM_Init(uint32_t freq_hz)
{
    uint32_t psc;
    uint32_t arr;
    uint32_t i;

    /* ---------- ① 开时钟: 不供时钟, 后面写 TIM3 的寄存器全是"白写" ---------- */
    RCC->APB2ENR |= (1u << 0)      /* bit0  AFIOEN : 复用功能时钟 */
                  | (1u << 2)      /* bit2  IOPAEN : GPIOA 时钟   */
                  | (1u << 3);     /* bit3  IOPBEN : GPIOB 时钟   */
    RCC->APB1ENR |= (1u << 1);     /* bit1  TIM3EN : TIM3 时钟    */

    /* ---------- ② 配引脚: 复用推挽输出 50MHz ----------
     * GPIOx->CRL 每 4 位控制一个引脚(这里 PA6/PA7):
     *     bit27:24 -> PA6       bit31:28 -> PA7
     *   4 位含义: [3:2] CNF = 10 复用推挽输出
     *             [1:0] MODE= 11 输出 50MHz
     *   合起来 0b1011 = 0xB
     */
    GPIOA->CRL &= ~(0xFFu << 24);  /* 先清零(清完才能写, 否则是"或"上旧值) */
    GPIOA->CRL |=  (0xBBu << 24);  /* PA6 = 0xB, PA7 = 0xB */

    /* PB0 -> bit3:0, PB1 -> bit7:4 */
    GPIOB->CRL &= ~0x000000FFu;
    GPIOB->CRL |=  0x000000BBu;

    /* ---------- ③ 配时基 ---------- */
    s_clk = PWM_GetTimerClock(TIM3);
    PWM_CalcPscArr(freq_hz, s_clk, &psc, &arr);
    s_period = arr + 1u;

    TIM3->PSC = (uint16_t)psc;     /* 预分频: 计数频率 = TIM3CLK/(PSC+1) */
    TIM3->ARR = (uint16_t)arr;     /* 自动重装: PWM 频率 = 计数频率/(ARR+1) */

    /* ---------- ④ 配通道模式: PWM 模式 1 + CCR 预装载 ----------
     * CCMR1 管 CH1/CH2, CCMR2 管 CH3/CH4, 两者位布局相同:
     *     bit6:4   OCxM[2:0] = 110  -> PWM 模式 1(计数值 < CCR 时输出高)
     *     bit3     OCxPE      = 1    -> CCR 走预装载(改占空比在周期边界生效)
     *     bit14:12 OC(x+1)M   = 110
     *     bit11    OC(x+1)PE  = 1
     */
    TIM3->CCMR1 = (uint16_t)((0x06u << 4) | (1u << 3) | (0x06u << 12) | (1u << 11));
    TIM3->CCMR2 = (uint16_t)((0x06u << 4) | (1u << 3) | (0x06u << 12) | (1u << 11));

    /* ---------- ⑤ 使能通道输出 ----------
     * CCER: bit0 CC1E, bit4 CC2E, bit8 CC3E, bit12 CC4E = 1
     *       极性位 CCxP 保持 0 -> 高电平有效(占空比指的就是"高电平"的比例)
     */
    TIM3->CCER = (1u << 0) | (1u << 4) | (1u << 8) | (1u << 12);

    /* ---------- ⑥ 预装载 + 启动 ---------- */
    TIM3->CR1 |= (1u << 7);        /* bit7 ARPE = 1: ARR 也走预装载 */
    TIM3->CCR1 = 0u;               /* 初始占空比 0% */
    TIM3->CCR2 = 0u;
    TIM3->CCR3 = 0u;
    TIM3->CCR4 = 0u;

    for (i = 0u; i < 4u; i++)
    {
        s_duty[i] = 0u;
    }

    TIM3->CR1 |= (1u << 0);        /* bit0 CEN = 1: 计数器开始计数 */
    TIM3->EGR |= (1u << 0);        /* bit0 UG  = 1: 立即更新事件, 把 CCR/ARR 装进影子寄存器 */
}

/**
 * @brief  运行中修改频率, 占空比(千分比)保持不变
 */
void PWM_SetFreq(uint32_t freq_hz)
{
    uint32_t psc;
    uint32_t arr;
    uint32_t i;

    PWM_CalcPscArr(freq_hz, s_clk, &psc, &arr);

    TIM3->PSC = (uint16_t)psc;     /* PSC 写入后下一个更新事件生效 */
    TIM3->ARR = (uint16_t)arr;     /* ARR 有预装载(ARPE=1), 同样在周期边界生效 */
    s_period  = arr + 1u;

    TIM3->EGR |= (1u << 0);        /* UG: 不等下一个周期, 立刻装载 */

    for (i = 0u; i < 4u; i++)      /* 周期变了, 4 个 CCR 要按新周期重算 */
    {
        PWM_SetDuty((uint8_t)(i + 1u), s_duty[i]);
    }
}

/**
 * @brief  设置占空比: 直接写 CCR 寄存器
 * @param  ch 1~4, duty 0~1000(千分比)
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

    /* CCR = duty/1000 × 周期计数 (四舍五入)
     * duty = 1000 时 CCR = ARR+1 > ARR -> 比较永远不匹配 -> 输出恒高 = 100% */
    ccr = (uint16_t)(((uint32_t)duty * s_period + (PWM_DUTY_FULL / 2u)) / PWM_DUTY_FULL);

    switch (ch)
    {
        case 1u: TIM3->CCR1 = ccr; break;   /* 通道 1 比较值寄存器 */
        case 2u: TIM3->CCR2 = ccr; break;   /* 通道 2 */
        case 3u: TIM3->CCR3 = ccr; break;   /* 通道 3 */
        case 4u: TIM3->CCR4 = ccr; break;   /* 通道 4 */
        default: break;
    }
}

/**
 * @brief  读回占空比(千分比)
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
 * @brief  读回实际频率: 频率 = 定时器时钟 / ((PSC+1) × (ARR+1))
 */
uint32_t PWM_GetActualFreq(void)
{
    return s_clk / ((TIM3->PSC + 1u) * s_period);
}

/* ==================== TIM1: 互补输出 + 死区 (纯寄存器) ==================== */
/**
 * @brief  初始化 TIM1: PA8 = CH1(主), PB13 = CH1N(互补), 并设置死区
 * @param  freq_hz      频率(Hz)
 * @param  deadtime_ns  死区(纳秒)
 */
void PWM_AdvInit(uint32_t freq_hz, uint32_t deadtime_ns)
{
    uint32_t psc;
    uint32_t arr;
    uint8_t  dtg;

    /* ---------- ① 时钟: GPIOA/GPIOB + TIM1(APB2 的 bit11) ---------- */
    RCC->APB2ENR |= (1u << 2) | (1u << 3) | (1u << 11);

    /* ---------- ② 引脚 ----------
     * GPIOx->CRH 每 4 位一个引脚, 引脚 n(8~15) 占 bit[(n-8)*4+3 : (n-8)*4]
     *     PA8  -> bit3:0    PB13 -> bit23:20
     *     值 0xB = [CNF=10 复用推挽][MODE=11 50MHz]
     */
    GPIOA->CRH &= ~0x0000000Fu;
    GPIOA->CRH |=  0x0000000Bu;          /* PA8  */
    GPIOB->CRH &= ~0x00F00000u;
    GPIOB->CRH |=  0x00B00000u;          /* PB13 */

    /* ---------- ③ 时基 ---------- */
    s_adv_clk = PWM_GetTimerClock(TIM1);
    PWM_CalcPscArr(freq_hz, s_adv_clk, &psc, &arr);
    s_adv_period = arr + 1u;

    TIM1->PSC = (uint16_t)psc;
    TIM1->ARR = (uint16_t)arr;

    /* ---------- ④ CH1 通道模式: PWM 模式 1 + 预装载 ---------- */
    TIM1->CCMR1 = (uint16_t)((0x06u << 4) | (1u << 3));   /* OC1M=110, OC1PE=1 */
    TIM1->CCR1  = 0u;

    /* ---------- ⑤ 互补输出使能 (CCER) ----------
     *  bit0  CC1E  = 1  主输出 PA8 使能
     *  bit1  CC1P  = 0  主输出高电平有效
     *  bit2  CC1NE = 1  互补输出 PB13 使能   <-- 通用定时器(TIM2~5)没有这一位
     *  bit3  CC1NP = 0  互补输出高电平有效
     */
    TIM1->CCER = (1u << 0) | (1u << 2);

    /* ---------- ⑥ BDTR: 死区 + 刹车 + 主输出 ---------- */
    dtg = PWM_DeadTimeFromNs(deadtime_ns, s_adv_clk, &s_adv_dead_ns);

    TIM1->BDTR = (uint32_t)dtg
               | (1u << 10)   /* OSSI = 1  MOE=0 时输出空闲电平              */
               | (1u << 11)   /* OSSR = 1  运行中关断时也进入空闲电平        */
               | (1u << 14)   /* AOE  = 1  刹车结束后自动恢复输出            */
               | (1u << 15);  /* MOE  = 1  主输出使能                        */
                              /* 高级定时器不置 MOE, PA8/PB13 一个波形都没有! */

    /* 空闲电平: CR2 的 bit8 OIS1(主) / bit9 OIS1N(互补) 复位值 0 = 空闲时输出低 */

    /* ---------- ⑦ 启动 ---------- */
    TIM1->CR1 |= (1u << 7);    /* bit7 ARPE = 1 预装载 */
    TIM1->CR1 |= (1u << 0);    /* bit0 CEN  = 1 开始计数 */
    TIM1->EGR |= (1u << 0);    /* bit0 UG   = 1 立刻产生更新事件 */
}

/**
 * @brief  设置互补对占空比: 直接写 TIM1->CCR1
 */
void PWM_AdvSetDuty(uint16_t duty)
{
    uint16_t ccr;

    if (duty > PWM_DUTY_FULL)
    {
        duty = PWM_DUTY_FULL;
    }
    s_adv_duty = duty;

    ccr = (uint16_t)(((uint32_t)duty * s_adv_period + (PWM_DUTY_FULL / 2u)) / PWM_DUTY_FULL);
    TIM1->CCR1 = ccr;
}

/**
 * @brief  读回实际死区时间(纳秒)
 */
uint32_t PWM_AdvGetDeadTimeNs(void)
{
    return s_adv_dead_ns;
}

/**
 * @brief  使能/关断互补输出(BDTR 的 MOE 位) —— 可作为 H 桥"急停"
 */
void PWM_AdvEnable(FunctionalState state)
{
    if (state != DISABLE)
    {
        TIM1->BDTR |=  (1u << 15);     /* MOE = 1 */
    }
    else
    {
        TIM1->BDTR &= ~(1u << 15);     /* MOE = 0, PA8/PB13 进入空闲电平(低) */
    }
}
