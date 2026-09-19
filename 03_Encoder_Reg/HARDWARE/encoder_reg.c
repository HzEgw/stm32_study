/**
 ******************************************************************************
 * @file    encoder_reg.c
 * @brief   编码器接口驱动 —— 纯寄存器实现
 *
 *  配置顺序(与标准库版一一对应):
 *    ① 时钟 ② PA6/PA7 上拉输入 ③ 时基 ④ CCMR1 通道输入+滤波
 *    ⑤ CCER 使能 ⑥ SMCR 编码器模式 ⑦ CNT/CR1 启动 ⑧ SysTick 节拍
 ******************************************************************************
 */

#include "encoder_reg.h"

/* ============================ 私有变量 ============================ */
static volatile uint16_t s_tick    = 0;
static volatile uint8_t  s_sample  = 0;

static int32_t  s_position = 0;
static int32_t  s_delta    = 0;
static int32_t  s_rpm      = 0;
static uint16_t s_last_cnt = 0;
static uint32_t s_samples  = 0;
static uint16_t s_pwm_duty = 0;
static uint32_t s_pwm_period = 1000u;

static void Encoder_ApplyPwm(uint16_t duty);

/* ============================ 初始化 ============================ */
void Encoder_Init(void)
{
    /* ---------- ① 时钟 ---------- */
    RCC->APB2ENR |= (1u << 0)      /* AFIOEN */
                  | (1u << 2);     /* IOPAEN */
    RCC->APB1ENR |= (1u << 1);     /* TIM3EN */

    /* ---------- ② PA6/PA7 = 上拉输入 ----------
     * GPIOA->CRL 每 4 位一个引脚: [3:2] CNF=10(输入上下拉) [1:0] MODE=00(输入) -> 0x8
     *   PA6 -> bit27:24    PA7 -> bit31:28
     * 再由 GPIOA->ODR 的对应位选"上拉"(1) 还是"下拉"(0)
     */
    GPIOA->CRL &= ~0xFF000000u;
    GPIOA->CRL |=  0x88000000u;
    GPIOA->ODR |= (1u << 6) | (1u << 7);      /* 上拉 */

    /* ---------- ③ 时基: 不分频(72MHz 计数, 分辨率最高), ARR 拉满 ---------- */
    TIM3->PSC = 0u;
    TIM3->ARR = 0xFFFFu;
    TIM3->CR1 &= ~(1u << 4);                  /* DIR = 0 (向上计数基准) */

    /* ---------- ④ CCMR1: 两个通道都设为"直接输入 + 最强滤波" ----------
     *  CC1S[1:0] = 01  IC1 <- TI1 (PA6)
     *  IC1F[3:0] = 1111 最强滤波(机械编码器抗抖)
     *  CC2S[1:0] = 01  IC2 <- TI2 (PA7)
     *  IC2F[3:0] = 1111
     */
    TIM3->CCMR1 = (uint16_t)((0x01u << 0)
                           | ((ENC_IC_FILTER & 0x0Fu) << 4)
                           | (0x01u << 8)
                           | ((ENC_IC_FILTER & 0x0Fu) << 12));   /* = 0xF1F1 */

    /* ---------- ⑤ CCER: 两个通道输入使能, 极性 0(上升沿) ---------- */
    TIM3->CCER = (1u << 0) | (1u << 4);

    /* ---------- ⑥ SMCR: SMS[2:0] = 011 编码器模式 3(TI12) ----------
     *   TI12 = A/B 两相的上升沿和下降沿都计数 => 四倍频, 方向由硬件判定
     *   (编码器模式 1 只数 TI2 边沿, 模式 2 只数 TI1 边沿, 模式 3 两相都数)
     */
    TIM3->SMCR = (uint16_t)((TIM3->SMCR & ~0x0007u) | 0x0003u);

    /* ---------- ⑦ 清零并启动 ---------- */
    TIM3->CNT = 0u;                           /* 位置从 0 开始 */
    TIM3->CR1 |= (1u << 0);                   /* CEN = 1 计数使能 */

    Encoder_Reset();

    /* ---------- ⑧ SysTick 1ms 节拍 ---------- */
    SysTick->LOAD = (SystemCoreClock / 1000u) - 1u;
    SysTick->VAL  = 0u;
    SysTick->CTRL = (1u << 2) | (1u << 1) | (1u << 0);   /* CLKSOURCE, TICKINT, ENABLE */
    NVIC_SetPriority(SysTick_IRQn, 3);
}

/* ==================== 转速 -> PWM(PB6) ==================== */
void Encoder_SpeedOutputInit(void)
{
    RCC->APB2ENR |= (1u << 3);        /* IOPBEN */
    RCC->APB1ENR |= (1u << 2);        /* TIM4EN */

    /* PB6 复用推挽输出 50MHz: GPIOB->CRL 的 bit27:24 = 0xB */
    GPIOB->CRL &= ~0x0F000000u;
    GPIOB->CRL |=  0x0B000000u;

    s_pwm_period = 1000000u / ENC_PWM_FREQ_HZ;      /* 1MHz 计数 */

    TIM4->PSC = 71u;                                 /* /72 -> 1MHz */
    TIM4->ARR = (uint16_t)(s_pwm_period - 1u);
    TIM4->CCMR1 = (uint16_t)((0x06u << 4) | (1u << 3));   /* OC1M=110 PWM1, OC1PE=1 */
    TIM4->CCER  = (1u << 0);                              /* CC1E = 1 */
    TIM4->CCR1  = 0u;
    TIM4->CR1 |= (1u << 7);                          /* ARPE */
    TIM4->CR1 |= (1u << 0);                          /* CEN  */
    TIM4->EGR |= (1u << 0);                          /* UG   */
}

static void Encoder_ApplyPwm(uint16_t duty)
{
    TIM4->CCR1 = (uint16_t)(((uint32_t)duty * s_pwm_period + 500u) / 1000u);
}

/* ============================ 采样与计算 ============================ */
/**
 * @brief  SysTick 1ms 中断: 只打节拍
 */
void Encoder_SysTickHandler(void)
{
    s_tick++;
    if (s_tick >= ENC_SAMPLE_MS)
    {
        s_tick   = 0;
        s_sample = 1u;            /* 计算交给主循环 */
    }
}

/**
 * @brief  主循环调用: 每 ENC_SAMPLE_MS 毫秒算一次位置增量与转速
 * @note   ★ 核心技巧: delta = (int16_t)(now - last)
 *         CNT 是 16 位, 从 0xFFFF 翻到 0 时用有符号减法依然得到正确的 +1,
 *         所以位置可以累加成 32 位而永不"跳变"。
 */
void Encoder_Update(void)
{
    uint16_t now;
    int32_t  delta;
    int32_t  rpm_abs;
    uint32_t duty;

    if (s_sample == 0u)
    {
        return;
    }
    s_sample = 0u;

    now   = (uint16_t)TIM3->CNT;                       /* 直接读寄存器 */
    delta = (int32_t)(int16_t)(now - s_last_cnt);      /* 有符号差值 */
    s_last_cnt = now;

    s_delta     = delta;
    s_position += delta;                               /* 32 位位置累加 */
    s_samples++;

    /* rpm = 增量 / 每转计数 × (60000 / 采样ms), 中间量用 64 位防溢出 */
    s_rpm = (int32_t)(((int64_t)delta * 60000) /
                      ((int64_t)ENC_COUNTS_PER_REV * ENC_SAMPLE_MS));

    /* 转速绝对值 -> PWM 占空比, 超过满量程饱和 */
    rpm_abs = (s_rpm < 0) ? -s_rpm : s_rpm;
    duty    = ((uint32_t)rpm_abs * 1000u) / ENC_PWM_FULL_RPM;
    if (duty > 1000u)
    {
        duty = 1000u;
    }
    s_pwm_duty = (uint16_t)duty;
    Encoder_ApplyPwm(s_pwm_duty);
}

/* ============================ 读取/复位 ============================ */
void Encoder_Reset(void)
{
    TIM3->CNT  = 0u;
    s_last_cnt = 0u;
    s_position = 0;
    s_delta    = 0;
    s_rpm      = 0;
    s_samples  = 0u;
}

int32_t Encoder_GetPosition(void) { return s_position; }
int32_t Encoder_GetDelta(void)    { return s_delta; }
int32_t Encoder_GetRpm(void)      { return s_rpm; }
uint32_t Encoder_GetSamples(void) { return s_samples; }
uint16_t Encoder_GetPwmDuty(void) { return s_pwm_duty; }

/**
 * @brief  方向: 读 TIM3->CR1 的 DIR(bit4), 硬件自动判定的结果
 */
uint8_t Encoder_GetDirection(void)
{
    return ((TIM3->CR1 & (1u << 4)) != 0u) ? 1u : 0u;   /* 0 = 正转, 1 = 反转 */
}
