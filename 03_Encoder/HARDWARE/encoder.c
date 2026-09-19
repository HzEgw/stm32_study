/**
 ******************************************************************************
 * @file    encoder.c
 * @brief   编码器接口驱动 (标准外设库)
 *
 *  三段式设计(工业代码常见做法):
 *    ① 编码器接口模式: 硬件四倍频, 它只改 CNT, 不产生任何 CPU 负担;
 *    ② SysTick 1ms 打节拍: 每 ENC_SAMPLE_MS 毫秒置一个"采样标志";
 *    ③ 主循环 Encoder_Update(): 看到标志才做除法运算 -> 中断里不做重活。
 *
 *  位置扩展: CNT 只有 16 位, 会溢出。用
 *        delta = (int16_t)(now - last);
 *    这个"有符号差值"算法规避了溢出问题(标准技巧), 再把 delta 累加到 32 位位置。
 ******************************************************************************
 */

#include "encoder.h"

/* ============================ 私有变量 ============================ */
static volatile uint16_t s_tick     = 0;     /* SysTick 1ms 计数 */
static volatile uint8_t  s_sample   = 0;     /* 1 = 该采样了     */

static int32_t  s_position  = 0;             /* 累计位置(计数) */
static int32_t  s_delta     = 0;             /* 本周期增量     */
static int32_t  s_rpm       = 0;             /* 转速           */
static uint16_t s_last_cnt  = 0;             /* 上次采样时的 CNT */
static uint32_t s_samples   = 0;             /* 采样次数       */
static uint16_t s_pwm_duty  = 0;             /* 送给 PB6 的占空比(千分比) */

/* ============================ 私有函数 ============================ */
static void     Encoder_ApplyPwm(uint16_t duty);

/* ============================ 初始化 ============================ */
/**
 * @brief  初始化 TIM3 为编码器接口模式 (PA6 = A 相, PA7 = B 相)
 */
void Encoder_Init(void)
{
    GPIO_InitTypeDef        GPIO_InitStructure;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_ICInitTypeDef       TIM_ICInitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);
    /* ↑ 实际寄存器操作: RCC->APB2ENR |= (1<<2)|(1<<0);   (IOPAEN / AFIOEN) */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    /* ↑ 实际寄存器操作: RCC->APB1ENR |= (1<<1);          (TIM3EN) */

    /* PA6/PA7 上拉输入(很多编码器是集电极开路输出, 需要上拉) */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    /* ↑ 等价于: GPIOA->CRL |= 0x88000000u;  GPIOA->ODR |= (1u<<6)|(1u<<7);
     *   (CNF=10 输入上下拉, ODR 的对应位=1 选上拉) */

    /* 时基: 预分频 0 -> 计数频率 = 72MHz(分辨率最高); ARR 拉满 */
    TIM_TimeBaseStructure.TIM_Prescaler         = 0;
    TIM_TimeBaseStructure.TIM_Period            = 0xFFFF;
    TIM_TimeBaseStructure.TIM_ClockDivision     = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode       = TIM_CounterMode_Up;
    TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);

    /* ---- 编码器接口模式: TI12 = A/B 两相的双边沿都计数(四倍频 + 自动判向) ----
     * 库函数实际写: TIM3->CCMR1 的 CC1S=01, CC2S=01
     *               TIM3->CCER  的 CC1E=1, CC2E=1 及极性位
     *               TIM3->SMCR  的 SMS[2:0]=011 (TI12 编码器模式)
     */
    TIM_EncoderInterfaceConfig(TIM3, TIM_EncoderMode_TI12,
                               TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);

    /* ---- 两个通道再加输入滤波(抗机械抖动/毛刺) ---- */
    TIM_ICInitStructure.TIM_Channel     = TIM_Channel_1;
    TIM_ICInitStructure.TIM_ICPolarity  = TIM_ICPolarity_Rising;
    TIM_ICInitStructure.TIM_ICSelection = TIM_ICSelection_DirectTI;
    TIM_ICInitStructure.TIM_ICPrescaler = TIM_ICPSC_DIV1;
    TIM_ICInitStructure.TIM_ICFilter    = ENC_IC_FILTER;
    TIM_ICInit(TIM3, &TIM_ICInitStructure);

    TIM_ICInitStructure.TIM_Channel = TIM_Channel_2;
    TIM_ICInit(TIM3, &TIM_ICInitStructure);

    TIM_SetCounter(TIM3, 0);             /* -> TIM3->CNT = 0 */
    TIM_Cmd(TIM3, ENABLE);               /* -> TIM3->CR1 |= (1<<0);  CEN */

    Encoder_Reset();

    /* SysTick 1ms 节拍: 只累加计数, 到点置标志 */
    SysTick_Config(SystemCoreClock / 1000u);
    NVIC_SetPriority(SysTick_IRQn, 3);
}

/* ============================ 转速 -> PWM 输出(可选, 便于万用表观察) ============================ */
static uint32_t s_pwm_period = 1000u;        /* TIM4 一个周期的计数(1MHz 下 1000 = 1kHz) */

/**
 * @brief  PB6(TIM4_CH1) 输出 1kHz PWM, 占空比与转速成正比
 * @note   转速 = ENC_PWM_FULL_RPM 时输出 100%; 这样把 PB6 接万用表(或 RC 滤波后)
 *         就能"看见"转速: 转得越快电压越高, 反向时... 仍是绝对值, 方向请看变量
 */
void Encoder_SpeedOutputInit(void)
{
    GPIO_InitTypeDef        GPIO_InitStructure;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_OCInitTypeDef       TIM_OCInitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);    /* -> RCC->APB2ENR |= (1<<3) */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);     /* -> RCC->APB1ENR |= (1<<2) */

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_6;              /* PB6 = TIM4_CH1 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    s_pwm_period = 1000000u / ENC_PWM_FREQ_HZ;               /* 1MHz 计数 */

    TIM_TimeBaseStructure.TIM_Prescaler         = 71u;       /* /72 -> 1MHz */
    TIM_TimeBaseStructure.TIM_Period            = (uint16_t)(s_pwm_period - 1u);
    TIM_TimeBaseStructure.TIM_ClockDivision     = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode       = TIM_CounterMode_Up;
    TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM4, &TIM_TimeBaseStructure);

    TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse       = 0;
    TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_High;
    TIM_OC1Init(TIM4, &TIM_OCInitStructure);
    TIM_OC1PreloadConfig(TIM4, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM4, ENABLE);

    TIM_Cmd(TIM4, ENABLE);
    TIM_GenerateEvent(TIM4, TIM_EventSource_Update);
}

static void Encoder_ApplyPwm(uint16_t duty)
{
    uint16_t ccr = (uint16_t)(((uint32_t)duty * s_pwm_period + 500u) / 1000u);
    TIM_SetCompare1(TIM4, ccr);          /* -> TIM4->CCR1 = ccr */
}

/* ============================ 采样与计算 ============================ */
/**
 * @brief  SysTick 1ms 中断: 只打节拍, 到 ENC_SAMPLE_MS 置一次采样标志
 */
void Encoder_SysTickHandler(void)
{
    s_tick++;
    if (s_tick >= ENC_SAMPLE_MS)
    {
        s_tick   = 0;
        s_sample = 1u;                   /* 真正的计算交给主循环 */
    }
}

/**
 * @brief  主循环里调用: 有采样标志时才算转速
 * @note   放主循环而不是中断里的原因: 这里要整除法和饱和处理, 中断应尽量短
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

    now   = (uint16_t)TIM_GetCounter(TIM3);              /* 读 TIM3->CNT */
    /* ★ 有符号差值: 即使 CNT 从 65535 翻到 0, 结果依然正确 */
    delta = (int32_t)(int16_t)(now - s_last_cnt);
    s_last_cnt = now;

    s_delta     = delta;
    s_position += delta;
    s_samples++;

    /* rpm = 增量/每转计数 × (60000 / 采样ms), 用 64 位中间量防止溢出 */
    s_rpm = (int32_t)(((int64_t)delta * 60000) /
                      ((int64_t)ENC_COUNTS_PER_REV * ENC_SAMPLE_MS));

    /* 转速绝对值 -> PWM 占空比(0~1000), 超过满量程就饱和 */
    rpm_abs = (s_rpm < 0) ? -s_rpm : s_rpm;
    duty    = ((uint32_t)rpm_abs * 1000u) / ENC_PWM_FULL_RPM;
    if (duty > 1000u)
    {
        duty = 1000u;
    }
    s_pwm_duty = (uint16_t)duty;
    Encoder_ApplyPwm(s_pwm_duty);
}

/* ============================ 对外读取/复位 ============================ */
void Encoder_Reset(void)
{
    TIM_SetCounter(TIM3, 0);
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
 * @brief  方向: 直接读 TIM3->CR1 的 DIR(bit4) —— 硬件自己判的方向
 */
uint8_t Encoder_GetDirection(void)
{
    return ((TIM3->CR1 & TIM_CR1_DIR) != 0u) ? 1u : 0u;   /* 0 = 正转, 1 = 反转 */
}
