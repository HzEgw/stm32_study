/**
 ******************************************************************************
 * @file    LED.c
 * @brief   板上 3 个 LED 的驱动 + 三种灯效（流水灯 / 呼吸灯 / 亮度波浪）
 *
 *  ============ 三个效果的实现思路（考核讲解重点） ============
 *   ① 流水灯：只需要"亮/灭" → 用 GPIO 普通推挽输出（GPIO 输出外设）
 *   ② 呼吸灯：只需要"亮度"   → 用 TIM2 PWM，三路 duty 同时按正弦变化
 *   ③ 波浪灯：需要"亮度 + 相位差" → 同一个正弦表，三路分别取相位 0° / 120° / 240°
 *
 *   ⭐ 关键：没有用 sin() 浮点函数！
 *      Cortex-M3 没有 FPU，用 float + sin() 会拖慢速度、还要额外链接数学库；
 *      这里用一个 120 点的"正弦表"（查表代替计算）：
 *        · 表长 120 = 一个完整周期，值域 0~1000（正好当千分比占空比用）
 *        · 120 / 3 = 40 点 = 精确 120° → 三相相位差不用凑数，刚好整除
 *      查表还有一个好处：以后想换"三角波/呼吸曲线"，只换这张表就行。
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "LED.h"
#include "PWM.h"

/* ===================== 引脚定义 ===================== */
#define LED_PORT       GPIOA
#define LED1_PIN       GPIO_Pin_1
#define LED2_PIN       GPIO_Pin_2
#define LED3_PIN       GPIO_Pin_3
#define LED_ALL_PIN    (LED1_PIN | LED2_PIN | LED3_PIN)

/* 流水灯：每走这么多小步换一个灯（10 × 20ms = 200ms 一个灯） */
#define FLOW_TICKS_PER_LED   10u

/* ===================== 正弦表（120 点，值 0~1000） ===================== */
#define SIN_LEN      120u                 /* 表长：一个完整周期 */
#define SIN_PHASE    40u                  /* 120° 相位 = 120/3 = 40 个点 */

static const uint16_t s_sin[SIN_LEN] =
{
    500,526,552,578,604,629,655,679,703,727,
    750,772,794,815,835,854,872,889,905,919,
    933,946,957,967,976,983,989,994,997,999,
    1000,999,997,994,989,983,976,967,957,946,
    933,919,905,889,872,854,835,815,794,772,
    750,727,703,679,655,629,604,578,552,526,
    500,474,448,422,396,371,345,321,297,273,
    250,228,206,185,165,146,128,111,95,81,
    67,54,43,33,24,17,11,6,3,1,
    0,1,3,6,11,17,24,33,43,54,
    67,81,95,111,128,146,165,185,206,228,
    250,273,297,321,345,371,396,422,448,474
};

/* ===================== 底层：引脚 + 亮灭 + 亮度 ===================== */

/**
 * @brief  把 3 个 LED 引脚配成普通的推挽输出（给"亮/灭"类效果用）
 * @note   写 1 = 引脚输出高 = LED 亮（本板 LED 是"高电平点亮"，和你原代码一致）
 *         -> 实际寄存器：GPIOA->CRL 的 PA1/PA2/PA3 三组 4 位 = 0x3（推挽输出 50MHz）
 */
void LED_GpioInit(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = LED_ALL_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;   /* 普通推挽输出 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LED_PORT, &GPIO_InitStructure);

    LED_AllOff();       /* 干净起步：不残留上一个效果留下的电平 */
}

/**
 * @brief  把 3 个 LED 引脚交给 TIM2 PWM（给"亮度"类效果用）
 */
void LED_PwmInit(void)
{
    PWM_Init();                 /* 内部会把 PA1/PA2/PA3 配成复用推挽 */
    LED_SetBrightness(0u, 0u);  /* 从"全灭"起步 */
    LED_SetBrightness(1u, 0u);
    LED_SetBrightness(2u, 0u);
}

/**
 * @brief  点亮第 idx 个灯（idx: 0~2）—— GPIO 模式下才有效
 */
void LED_On(uint8_t idx)
{
    switch (idx)
    {
        case 0u: GPIO_SetBits(LED_PORT, LED1_PIN);   break;   /* -> GPIOA->BSRR = pin */
        case 1u: GPIO_SetBits(LED_PORT, LED2_PIN);   break;
        case 2u: GPIO_SetBits(LED_PORT, LED3_PIN);   break;
        default: break;
    }
}

/**
 * @brief  熄灭第 idx 个灯（idx: 0~2）
 */
void LED_Off(uint8_t idx)
{
    switch (idx)
    {
        case 0u: GPIO_ResetBits(LED_PORT, LED1_PIN); break;   /* -> GPIOA->BRR = pin */
        case 1u: GPIO_ResetBits(LED_PORT, LED2_PIN); break;
        case 2u: GPIO_ResetBits(LED_PORT, LED3_PIN); break;
        default: break;
    }
}

/**
 * @brief  三个灯全灭
 * @note   库函数 GPIO_ResetBits 一次可以操作多位，所以一行就够
 */
void LED_AllOff(void)
{
    GPIO_ResetBits(LED_PORT, LED_ALL_PIN);
}

/**
 * @brief  设置第 idx 个灯的亮度（idx: 0~2, duty: 0~1000）—— PWM 模式下才有效
 */
void LED_SetBrightness(uint8_t idx, uint16_t duty)
{
    switch (idx)
    {
        case 0u: PWM_SetCompare(PWM_CH_LED1, duty); break;    /* PA1 <- CCR2 */
        case 1u: PWM_SetCompare(PWM_CH_LED2, duty); break;    /* PA2 <- CCR3 */
        case 2u: PWM_SetCompare(PWM_CH_LED3, duty); break;    /* PA3 <- CCR4 */
        default: break;
    }
}

/**
 * @brief  读回第 idx 个灯当前亮度（idx: 0~2，返回值 0~1000）
 * @note   返回值其实就是 CCR 寄存器，直接看硬件真实值，适合放 Watch 观察
 */
uint16_t LED_GetBrightness(uint8_t idx)
{
    switch (idx)
    {
        case 0u: return PWM_GetCompare(PWM_CH_LED1);
        case 1u: return PWM_GetCompare(PWM_CH_LED2);
        case 2u: return PWM_GetCompare(PWM_CH_LED3);
        default: return 0u;
    }
}

/* ==========================================================================
 *  三个效果：Init = 进入时配置一次；Step = 走一小步立刻返回
 *  （static 变量 = 这个效果自己的"进度状态"，别人看不见、也不会互相干扰）
 * ========================================================================== */

/* ===================== 效果 ①：传统流水灯 ===================== */
/*  现象：单个亮点从左往右跑，200ms 换一个灯，跑完回头
 *  实现：不需要 PWM —— 只要"亮/灭"，所以引脚用普通推挽输出即可
 */
static uint8_t s_flow_led;      /* 当前亮的是第几个灯（0~2） */
static uint8_t s_flow_tick;     /* 计时：攒够 FLOW_TICKS_PER_LED 就换灯 */

void Effect_Flow_Init(void)
{
    LED_GpioInit();             /* ① 引脚切回"普通推挽输出"（关键！） */
    s_flow_led  = 0u;
    s_flow_tick = 0u;
    LED_On(s_flow_led);         /* ② 立刻亮第一个灯，演示时不用先黑 200ms */
}

void Effect_Flow_Step(void)
{
    s_flow_tick++;
    if (s_flow_tick < FLOW_TICKS_PER_LED)
    {
        return;                 /* 还没到换灯时间，直接回去（这样按键才不会被延时挡住） */
    }
    s_flow_tick = 0u;

    LED_AllOff();               /* 先全灭，形成"只有一个亮点在跑"的效果 */
    s_flow_led++;
    if (s_flow_led >= LED_COUNT)
    {
        s_flow_led = 0u;        /* 跑完一轮从第一个灯重新开始 */
    }
    LED_On(s_flow_led);
}

/* ===================== 效果 ②：呼吸灯（三灯同步 渐亮渐灭） ===================== */
/*  现象：3 个灯一起由暗变亮、再由亮变暗，像呼吸
 *  实现：三路 duty 取同一个正弦值 —— 一个周期 = 120 步 × 20ms = 2.4s
 */
static uint16_t s_bre_idx;      /* 正弦表下标 = 当前相位 */

void Effect_Breathe_Init(void)
{
    LED_PwmInit();              /* ① 引脚切成复用输出 + 启动 TIM2 PWM */
    s_bre_idx = 0u;             /* ② 从相位 0（半亮）开始 */
}

void Effect_Breathe_Step(void)
{
    uint16_t bright = s_sin[s_bre_idx];

    LED_SetBrightness(0u, bright);      /* 三个灯给同一个亮度 = 同步呼吸 */
    LED_SetBrightness(1u, bright);
    LED_SetBrightness(2u, bright);

    s_bre_idx++;
    if (s_bre_idx >= SIN_LEN)
    {
        s_bre_idx = 0u;                 /* 相位绕回 0，正弦表首尾相接，看不出接缝 */
    }
}

/* ===================== 效果 ③：亮度波浪（正弦，左 → 右） ===================== */
/*  现象：亮峰从 LED1 依次流到 LED2、LED3，像波浪从左往右推
 *  实现：三个灯读同一张表，但 LED2 / LED3 各"延迟" 40 / 80 个点（= 120° / 240°）
 *        → 峰值依次晚到 40 步（0.8s），所以看上去亮峰是在往右跑
 */
static uint16_t s_wave_idx;     /* 当前相位（LED1 的相位） */

void Effect_Wave_Init(void)
{
    LED_PwmInit();
    s_wave_idx = 0u;
}

void Effect_Wave_Step(void)
{
    /* 三相相差 120°（= 120/3 = 40 个点）：让 LED2、LED3 各"晚" 40、80 个点到达峰值
     * → 亮峰先到 LED1，0.8s 后到 LED2，再 0.8s 到 LED3 = 从左往右推
     *   （查表下标 = 当前相位 - 延迟量，绕回时 +120；因为 idx ∈ [0,119]，
     *     最大只到 119+80，所以减一次 120 就够，不用取模运算） */
    uint16_t i1 = s_wave_idx;                                   /* LED1：延迟 0  个点 */
    uint16_t i2 = s_wave_idx + (SIN_LEN - SIN_PHASE);            /* LED2：延迟 40 个点 = 120° */
    uint16_t i3 = s_wave_idx + (SIN_LEN - 2u * SIN_PHASE);       /* LED3：延迟 80 个点 = 240° */

    if (i2 >= SIN_LEN) { i2 -= SIN_LEN; }           /* 下标绕回（查表不允许越界） */
    if (i3 >= SIN_LEN) { i3 -= SIN_LEN; }

    LED_SetBrightness(0u, s_sin[i1]);
    LED_SetBrightness(1u, s_sin[i2]);
    LED_SetBrightness(2u, s_sin[i3]);

    s_wave_idx++;
    if (s_wave_idx >= SIN_LEN)
    {
        s_wave_idx = 0u;
    }
}
