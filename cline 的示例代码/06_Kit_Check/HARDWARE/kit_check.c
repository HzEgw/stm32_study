/**
 ******************************************************************************
 * @file    kit_check.c
 * @brief   套件到货验收工具实现（标准库版）
 *
 *   ① 串口打印(轮询)  ② LED/按键  ③ 电机(双 PWM) + 编码器
 *   ④ 电池电压 ADC    ⑤ I2C 扫描  ⑥ 一键自检流程
 *
 *  ★ 设计原则：**每个动作都有可观察的证据**
 *     - 电机: 不仅能转, 还要用编码器读数反过来验证"转的方向对不对"
 *     - 编码器: 能读出"手转一圈数多少格", 直接算出真实的 CANTS_PER_REV
 *     - 电池: 给出 mV, 顺便验证分压系数对不对
 ******************************************************************************
 */

#include "kit_check.h"
#include "kit_config.h"

/* ============================ 全局(Watch 观察点) ============================ */
volatile uint32_t g_kit_tick         = 0;
volatile uint8_t  g_kit_pass[8]      = {0};
volatile uint8_t  g_kit_pass_cnt     = 0;
volatile uint8_t  g_kit_scan_cnt     = 0;
volatile uint8_t  g_kit_scan_list[8] = {0};
volatile int32_t  g_kit_enc_total[2] = {0, 0};
volatile int32_t  g_kit_enc_delta[2] = {0, 0};
volatile int32_t  g_kit_rpm_x100[2]  = {0, 0};
volatile uint16_t g_kit_bat_mv       = 0;
volatile uint8_t  g_kit_dir_ok       = 0;

/* 私有变量 */
static int16_t s_enc_last[2] = {0, 0};      /* 上次读到的 16 位计数(有符号) */

/* ============================ ① 串口打印(只发不收) ============================ */
/**
 * @brief  USART1: PA9=TX, PA10=RX, 115200 8N1
 * @note   只用轮询发送, 不开中断 —— 验收工具不需要接收
 *         BRR = PCLK2(72MHz) / 115200 = 625 = 0x271
 */
void Kit_UartInit(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_9;             /* TX 复用推挽 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_10;             /* RX 浮空输入 */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate            = 115200u;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART1, &USART_InitStructure);

    USART_Cmd(USART1, ENABLE);
}

/* 单字符输出(内部用) */
static void kit_uart_putc(char c)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) { }
    USART_SendData(USART1, (uint16_t)c);
}

void Kit_Print(const char *s)
{
    while (*s != '\0')
    {
        kit_uart_putc(*s);
        s++;
    }
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET) { }
}

void Kit_PrintU32(uint32_t v)
{
    char    tmp[11];
    uint8_t i = 0u;

    if (v == 0u) { kit_uart_putc('0'); return; }
    while ((v != 0u) && (i < 10u))
    {
        tmp[i] = (char)('0' + (v % 10u));
        v /= 10u;
        i++;
    }
    while (i != 0u)
    {
        i--;
        kit_uart_putc(tmp[i]);
    }
}

void Kit_PrintI32(int32_t v)
{
    if (v < 0)
    {
        Kit_Print("-");
        Kit_PrintU32((uint32_t)(-v));
    }
    else
    {
        Kit_PrintU32((uint32_t)v);
    }
}

/* ============================ ② 延时 / LED / 按键 ============================ */
/**
 * @brief  基于 SysTick 的 1ms 节拍延时(不空转 CPU)
 */
void Kit_DelayMs(uint32_t ms)
{
    uint32_t start = g_kit_tick;

    while ((uint32_t)(g_kit_tick - start) < ms) { }
}

void Kit_LedSet(uint8_t on)
{
#if (KIT_LED_ACTIVE_LOW == 1u)
    if (on != 0u) { GPIO_ResetBits(KIT_LED_PORT, KIT_LED_PIN); }   /* 低电平点亮 */
    else          { GPIO_SetBits(KIT_LED_PORT, KIT_LED_PIN); }
#else
    if (on != 0u) { GPIO_SetBits(KIT_LED_PORT, KIT_LED_PIN); }
    else          { GPIO_ResetBits(KIT_LED_PORT, KIT_LED_PIN); }
#endif
}

uint8_t Kit_KeyPressed(void)
{
    /* 上拉输入: 按下 = 低电平 */
    return (GPIO_ReadInputDataBit(KIT_KEY_PORT, KIT_KEY_PIN) == Bit_RESET) ? 1u : 0u;
}

/**
 * @brief  初始化 LED 与按键
 */
static void kit_gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = KIT_LED_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_Init(KIT_LED_PORT, &GPIO_InitStructure);
    Kit_LedSet(0u);

    GPIO_InitStructure.GPIO_Pin  = KIT_KEY_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;      /* 上拉输入 */
    GPIO_Init(KIT_KEY_PORT, &GPIO_InitStructure);
}

/* ============================ ③ 电机 (PWM + 方向) ============================ */
/**
 * @brief  TIM2 输出两路 PWM: CH1 = PA0(电机1), CH3 = PA2(电机2)
 * @note   20kHz: PSC = 71 → 1MHz 计数, ARR = 49 → 1MHz/50 = 20kHz
 *         占空比换算: CCR = duty × (ARR+1) / 1000
 *
 *  ★ 与鱼香官方固件的对照:
 *       官方: MCPWM 输出 A/B 两路, 一路给占空比、一路拉低(5kHz)
 *       本工程: 一个脚 PWM + 另一个脚作方向脚(20kHz) —— 对 TB6612/L298N 都通用
 *       想复刻"双 PWM"写法: 把方向脚也配成 PWM(占空比 0) 即可
 */
static void kit_motor_init(void)
{
    GPIO_InitTypeDef        GPIO_InitStructure;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_OCInitTypeDef       TIM_OCInitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    /* PA0/PA2 = PWM 复用推挽;  PA1/PA3 = 方向脚 推挽输出 */
    GPIO_InitStructure.GPIO_Pin   = KIT_M1_PWM_PIN | KIT_M2_PWM_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin  = KIT_M1_DIR_PIN | KIT_M2_DIR_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    TIM_TimeBaseStructure.TIM_Prescaler         = 71u;    /* 72MHz/(71+1) = 1MHz */
    TIM_TimeBaseStructure.TIM_Period            = (1000000u / KIT_PWM_FREQ_HZ) - 1u;
    TIM_TimeBaseStructure.TIM_ClockDivision     = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode       = TIM_CounterMode_Up;
    TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

    TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse       = 0u;
    TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_High;

    TIM_OC1Init(TIM2, &TIM_OCInitStructure);              /* CH1 = PA0 */
    TIM_OC1PreloadConfig(TIM2, TIM_OCPreload_Enable);
    TIM_OC3Init(TIM2, &TIM_OCInitStructure);              /* CH3 = PA2 */
    TIM_OC3PreloadConfig(TIM2, TIM_OCPreload_Enable);

    TIM_ARRPreloadConfig(TIM2, ENABLE);
    TIM_Cmd(TIM2, ENABLE);
    TIM_GenerateEvent(TIM2, TIM_EventSource_Update);
}

/**
 * @brief  设置电机转速(带方向)
 * @param  id   0 = 电机1(PA0/PA1), 1 = 电机2(PA2/PA3)
 * @param  duty -1000 ~ +1000 千分比; 正 = 正转
 */
void Kit_MotorSet(uint8_t id, int16_t duty)
{
    uint16_t ccr;
    uint8_t  forward;

    if (duty >  (int16_t)KIT_DUTY_MAX) { duty = (int16_t)KIT_DUTY_MAX; }
    if (duty < -(int16_t)KIT_DUTY_MAX) { duty = -(int16_t)KIT_DUTY_MAX; }

    forward = (duty >= 0) ? 1u : 0u;
    if (duty < 0) { duty = (int16_t)(-duty); }

    ccr = (uint16_t)(((uint32_t)duty * (1000000u / KIT_PWM_FREQ_HZ)) / (uint32_t)KIT_DUTY_MAX);

    if (id == 0u)
    {
        if (forward != 0u) { GPIO_ResetBits(KIT_M1_DIR_PORT, KIT_M1_DIR_PIN); }
        else               { GPIO_SetBits(KIT_M1_DIR_PORT, KIT_M1_DIR_PIN); }
        TIM_SetCompare1(TIM2, ccr);
    }
    else
    {
        if (forward != 0u) { GPIO_ResetBits(KIT_M2_DIR_PORT, KIT_M2_DIR_PIN); }
        else               { GPIO_SetBits(KIT_M2_DIR_PORT, KIT_M2_DIR_PIN); }
        TIM_SetCompare3(TIM2, ccr);
    }
}

/* ============================ ④ 编码器 ============================ */
/**
 * @brief  TIM3(PA6/PA7) + TIM4(PB6/PB7) 编码器接口模式, TI12 = **两相四倍频**
 * @note   ⚠️ 顺序很关键: 先 TIM_ICInit 设**滤波**, 再 TIM_EncoderInterfaceConfig 设**编码器模式**
 *         (反过来的话, TIM_ICInit 会把 CC1S/CC2S 覆盖掉, 编码器模式就失效了)
 */
static void kit_encoder_init(void)
{
    GPIO_InitTypeDef        GPIO_InitStructure;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_ICInitTypeDef       TIM_ICInitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3 | RCC_APB1Periph_TIM4, ENABLE);

    /* A/B 相都用"上拉输入"(霍尔编码器多为集电极开路/开漏输出) */
    GPIO_InitStructure.GPIO_Pin   = KIT_ENC1_A_PIN | KIT_ENC1_B_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_Init(KIT_ENC1_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = KIT_ENC2_A_PIN | KIT_ENC2_B_PIN;
    GPIO_Init(KIT_ENC2_PORT, &GPIO_InitStructure);

    TIM_TimeBaseStructure.TIM_Prescaler         = 0u;
    TIM_TimeBaseStructure.TIM_Period            = 0xFFFFu;   /* 16 位满量程 */
    TIM_TimeBaseStructure.TIM_ClockDivision     = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode       = TIM_CounterMode_Up;
    TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);
    TIM_TimeBaseInit(TIM4, &TIM_TimeBaseStructure);

    /* ① 先设输入滤波(机械/Hall 编码器开到最强, 抗抖动) */
    TIM_ICInitStructure.TIM_ICPolarity  = TIM_ICPolarity_Rising;
    TIM_ICInitStructure.TIM_ICSelection = TIM_ICSelection_DirectTI;
    TIM_ICInitStructure.TIM_ICPrescaler = TIM_ICPSC_DIV1;
    TIM_ICInitStructure.TIM_ICFilter    = 0x0Fu;

    TIM_ICInitStructure.TIM_Channel = TIM_Channel_1;  TIM_ICInit(TIM3, &TIM_ICInitStructure);
    TIM_ICInitStructure.TIM_Channel = TIM_Channel_2;  TIM_ICInit(TIM3, &TIM_ICInitStructure);
    TIM_ICInitStructure.TIM_Channel = TIM_Channel_1;  TIM_ICInit(TIM4, &TIM_ICInitStructure);
    TIM_ICInitStructure.TIM_Channel = TIM_Channel_2;  TIM_ICInit(TIM4, &TIM_ICInitStructure);

    /* ② 再开编码器模式(TI12 = A/B 两相双边沿 → 4 倍频, 与官方 PulseRatio=44 一致) */
    TIM_EncoderInterfaceConfig(TIM3, TIM_EncoderMode_TI12, TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);
    TIM_EncoderInterfaceConfig(TIM4, TIM_EncoderMode_TI12, TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);

    TIM_SetCounter(TIM3, 0u);
    TIM_SetCounter(TIM4, 0u);
    TIM_Cmd(TIM3, ENABLE);
    TIM_Cmd(TIM4, ENABLE);
}

int32_t Kit_EncoderGet(uint8_t id)
{
    return (id == 0u) ? g_kit_enc_total[0] : g_kit_enc_total[1];
}

int32_t Kit_EncoderDelta(uint8_t id)
{
    return (id == 0u) ? g_kit_enc_delta[0] : g_kit_enc_delta[1];
}

int32_t Kit_EncoderRpmX100(uint8_t id)
{
    return (id == 0u) ? g_kit_rpm_x100[0] : g_kit_rpm_x100[1];
}

/**
 * @brief  读一次编码器: 处理 16 位回绕 → 累计位置 → 算转速
 * @note   每 KIT_ENC_SAMPLE_MS(默认 10ms) 调用一次
 *
 *  ★ 16 位回绕的处理: 把 CNT 当"有符号数", 用有符号差值累加即可 ——
 *    对比 ESP32 官方固件: PCNT 靠 ±100 上限 + 中断累加, 这里更简洁。
 */
void Kit_EncoderUpdate(void)
{
    int16_t now;
    uint8_t id;

    for (id = 0u; id < 2u; id++)
    {
        now = (id == 0u) ? (int16_t)TIM3->CNT : (int16_t)TIM4->CNT;

        g_kit_enc_delta[id] = (int32_t)(int16_t)(now - s_enc_last[id]);
        s_enc_last[id]      = now;
        g_kit_enc_total[id] += g_kit_enc_delta[id];

        /* rpm = Δcount / 每圈计数 × (60000 / 采样周期ms); ×100 存整数, Watch 里看得清 */
        g_kit_rpm_x100[id] = (int32_t)(((float)g_kit_enc_delta[id] * 6000000.0f)
                                       / (KIT_COUNTS_PER_REV * (float)KIT_ENC_SAMPLE_MS));
    }
}

/* ============================ ⑤ 电池电压 (ADC1_IN4 / PA4) ============================ */
/**
 * @brief  ADC1 单次转换, 用来读"分压后的电池电压"
 * @note   ADCCLK = PCLK2/6 = 12MHz (手册要求 ≤14MHz)
 *         换算: 引脚电压(mV) = adc × VREF / 4096 ; 电池电压 = 引脚电压 × 分压系数
 */
static void kit_adc_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    ADC_InitTypeDef  ADC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);

    GPIO_InitStructure.GPIO_Pin  = KIT_ADC_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;        /* 模拟输入 */
    GPIO_Init(KIT_ADC_PORT, &GPIO_InitStructure);

    ADC_InitStructure.ADC_Mode               = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode       = DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStructure.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign          = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel       = 1u;
    ADC_Init(ADC1, &ADC_InitStructure);

    ADC_Cmd(ADC1, ENABLE);

    /* 校准: 必须先"复位校准"再"启动校准", 否则头几次数据不准 */
    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1) == SET) { }
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1) == SET) { }
}

uint16_t Kit_BatteryMv(void)
{
    uint32_t adc;
    uint32_t mv;

    ADC_RegularChannelConfig(ADC1, KIT_ADC_CHANNEL, 1u, ADC_SampleTime_55Cycles5);
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET) { }
    adc = (uint32_t)ADC_GetConversionValue(ADC1);
    ADC_ClearFlag(ADC1, ADC_FLAG_EOC);

    mv = (adc * KIT_ADC_VREF_MV * KIT_ADC_DIV_NUM) / (4096u * KIT_ADC_DIV_DEN);
    g_kit_bat_mv = (uint16_t)mv;
    return (uint16_t)mv;
}

/* ============================ ⑥ I2C2 扫描 (PB10/PB11) ============================ */
/* 十六进制打印(2 位, 内部用) */
static void kit_print_hex8(uint8_t v)
{
    const char *hex = "0123456789ABCDEF";

    kit_uart_putc(hex[(v >> 4) & 0x0Fu]);
    kit_uart_putc(hex[v & 0x0Fu]);
}

/**
 * @brief  I2C2: PB10 = SCL, PB11 = SDA, 100kHz（扫描用低速更稳）
 */
static void kit_i2c_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    I2C_InitTypeDef  I2C_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C2, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = KIT_I2C_SCL_PIN | KIT_I2C_SDA_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_OD;      /* 复用开漏(必须有上拉) */
    GPIO_Init(KIT_I2C_PORT, &GPIO_InitStructure);

    I2C_InitStructure.I2C_Mode                = I2C_Mode_I2C;
    I2C_InitStructure.I2C_DutyCycle           = I2C_DutyCycle_2;
    I2C_InitStructure.I2C_OwnAddress1         = 0x00u;
    I2C_InitStructure.I2C_Ack                 = I2C_Ack_Enable;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_InitStructure.I2C_ClockSpeed          = 100000u;
    I2C_Init(I2C2, &I2C_InitStructure);
    I2C_Cmd(I2C2, ENABLE);
    I2C_AcknowledgeConfig(I2C2, ENABLE);
}

/**
 * @brief  探测某个 7 位地址(只发地址, 随即 STOP); 0 = 有应答
 * @note   全程带超时; 无应答是正常现象, 不计入错误
 */
static uint8_t kit_i2c_probe(uint8_t addr7)
{
    uint32_t t;

    I2C_GenerateSTART(I2C2, ENABLE);
    t = 200000u;
    while ((I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_MODE_SELECT) == ERROR) && (--t != 0u)) { }
    if (t == 0u) { I2C_GenerateSTOP(I2C2, ENABLE); return 1u; }

    I2C_Send7bitAddress(I2C2, (uint8_t)(addr7 << 1), I2C_Direction_Transmitter);

    t = 200000u;
    while ((t != 0u)
           && (I2C_GetFlagStatus(I2C2, I2C_FLAG_ADDR) == RESET)
           && (I2C_GetFlagStatus(I2C2, I2C_FLAG_AF)   == RESET)) { t--; }

    if (I2C_GetFlagStatus(I2C2, I2C_FLAG_ADDR) != RESET)
    {
        (void)I2C2->SR1;              /* 清 ADDR: 先读 SR1 */
        (void)I2C2->SR2;              /*           再读 SR2 */
        I2C_GenerateSTOP(I2C2, ENABLE);
        return 0u;
    }

    I2C_ClearFlag(I2C2, I2C_FLAG_AF);
    I2C_GenerateSTOP(I2C2, ENABLE);
    return 1u;
}

uint8_t Kit_I2CScan(uint8_t *list, uint8_t max)
{
    uint8_t addr;
    uint8_t cnt = 0u;

    for (addr = 0x08u; addr <= 0x77u; addr++)
    {
        if (kit_i2c_probe(addr) == 0u)
        {
            if (cnt < max) { list[cnt] = addr; }
            cnt++;
        }
    }

    for (addr = 0u; (addr < cnt) && (addr < 8u); addr++)
    {
        g_kit_scan_list[addr] = list[addr];
    }
    g_kit_scan_cnt = cnt;
    return cnt;
}

uint8_t Kit_ImuPresent(void)
{
    if (kit_i2c_probe(KIT_IMU_ADDR1) == 0u) { return 1u; }
    if (kit_i2c_probe(KIT_IMU_ADDR2) == 0u) { return 1u; }
    return 0u;
}

/* ============================ ⑦ 初始化 & 一键自检 ============================ */
/**
 * @brief  初始化全部外设
 * @note   调用前请先执行 SystemInit() 与 SysTick_Config(SystemCoreClock/1000)：
 *         本工程用 1ms 节拍做延时与采样节拍(见 main.c)。
 */
void Kit_Init(void)
{
    kit_gpio_init();
    Kit_UartInit();
    kit_motor_init();
    kit_encoder_init();
    kit_adc_init();
    kit_i2c_init();

    s_enc_last[0] = (int16_t)TIM3->CNT;
    s_enc_last[1] = (int16_t)TIM4->CNT;
}

uint8_t Kit_SelfTest(void)
{
    uint8_t  i;
    uint8_t  n;
    uint8_t  list[8];
    int32_t  d_fwd;
    int32_t  d_rev;
    uint32_t t0;
    uint16_t mv;

    for (i = 0u; i < 8u; i++) { g_kit_pass[i] = 0u; }
    g_kit_pass_cnt = 0u;
    g_kit_dir_ok   = 0u;

    Kit_Print("\r\n========= 套件验收自检开始 =========\r\n");

    /* ---------------- ① LED ---------------- */
    Kit_Print("[1/6] LED: 应闪 3 次(人眼确认)\r\n");
    for (i = 0u; i < 3u; i++)
    {
        Kit_LedSet(1u); Kit_DelayMs(150u);
        Kit_LedSet(0u); Kit_DelayMs(150u);
    }
    g_kit_pass[KIT_IDX_LED] = 1u;                 /* LED 靠人眼, 程序只保证"已执行" */

    /* ---------------- ② 按键 ---------------- */
    Kit_Print("[2/6] 按键: 请在 3 秒内按一下按键...\r\n");
    t0 = g_kit_tick;
    while ((uint32_t)(g_kit_tick - t0) < KIT_KEY_WAIT_MS)
    {
        if (Kit_KeyPressed() != 0u) { g_kit_pass[KIT_IDX_KEY] = 1u; break; }
    }
    if (g_kit_pass[KIT_IDX_KEY] != 0u) { Kit_Print("      按键 OK\r\n"); }
    else                               { Kit_Print("      X 没检测到按键(查按键另一端是否接 GND)\r\n"); }

    /* ---------------- ③ I2C 扫描 ---------------- */
    Kit_Print("[3/6] I2C2 扫描 0x08~0x77 ...\r\n");
    n = Kit_I2CScan(list, 8u);
    Kit_Print("      发现器件数: "); Kit_PrintU32(n); Kit_Print("\r\n");
    for (i = 0u; (i < n) && (i < 8u); i++)
    {
        Kit_Print("        0x"); kit_print_hex8(list[i]);
        if ((list[i] == KIT_IMU_ADDR1) || (list[i] == KIT_IMU_ADDR2)) { Kit_Print("  <- MPU6050(IMU)"); }
        if (list[i] == 0x3Cu) { Kit_Print("  <- OLED(SSD1306)"); }
        Kit_Print("\r\n");
    }
    g_kit_pass[KIT_IDX_I2C] = (n != 0u) ? 1u : 0u;

    /* ---------------- ④ 编码器(要人转轮子) ---------------- */
    Kit_Print("[4/6] 编码器: 3 秒内用手把**两个轮子**各转几圈...\r\n");
    {
        int32_t b0 = g_kit_enc_total[0];
        int32_t b1 = g_kit_enc_total[1];

        t0 = g_kit_tick;
        while ((uint32_t)(g_kit_tick - t0) < KIT_ENC_HAND_TEST_MS)
        {
            Kit_EncoderUpdate();
            Kit_DelayMs(KIT_ENC_SAMPLE_MS);
        }

        Kit_Print("      编码器1 增量: "); Kit_PrintI32(g_kit_enc_total[0] - b0);
        Kit_Print(" , 编码器2 增量: ");   Kit_PrintI32(g_kit_enc_total[1] - b1);
        Kit_Print("\r\n");

        g_kit_pass[KIT_IDX_ENCODER] = (((g_kit_enc_total[0] - b0) != 0) && ((g_kit_enc_total[1] - b1) != 0)) ? 1u : 0u;
    }

    /* ---------------- ⑤ 电机 + 转向一致性(用电编码器自动判定) ---------------- */
    Kit_Print("[5/6] 电机: 每个电机 正转1s → 停 → 反转1s\r\n");
    {
        uint8_t ok_all = 1u;

        for (i = 0u; i < 2u; i++)
        {
            int32_t e0 = g_kit_enc_total[i];

            Kit_MotorSet(i, (int16_t)KIT_MOTOR_TEST_DUTY);
            t0 = g_kit_tick;
            while ((uint32_t)(g_kit_tick - t0) < KIT_MOTOR_TEST_MS)
            {
                Kit_EncoderUpdate();
                Kit_DelayMs(KIT_ENC_SAMPLE_MS);
            }
            Kit_MotorSet(i, 0);
            Kit_DelayMs(300u);
            d_fwd = g_kit_enc_total[i] - e0;

            e0 = g_kit_enc_total[i];
            Kit_MotorSet(i, (int16_t)(-KIT_MOTOR_TEST_DUTY));
            t0 = g_kit_tick;
            while ((uint32_t)(g_kit_tick - t0) < KIT_MOTOR_TEST_MS)
            {
                Kit_EncoderUpdate();
                Kit_DelayMs(KIT_ENC_SAMPLE_MS);
            }
            Kit_MotorSet(i, 0);
            Kit_DelayMs(300u);
            d_rev = g_kit_enc_total[i] - e0;

            Kit_Print("      电机"); Kit_PrintU32((uint32_t)(i + 1u));
            Kit_Print(": 正转增量 "); Kit_PrintI32(d_fwd);
            Kit_Print(" , 反转增量 "); Kit_PrintI32(d_rev);

            if ((d_fwd == 0) || (d_rev == 0))
            {
                Kit_Print("  X 增量为 0 → 电机没转 或 编码器没接\r\n");
                ok_all = 0u;
            }
            else if ((d_fwd > 0) && (d_rev < 0))
            {
                Kit_Print("  -> 转向与编码器方向一致\r\n");
            }
            else
            {
                Kit_Print("  X 方向反了(交换该电机两根线, 或交换编码器 A/B)\r\n");
                ok_all = 0u;
            }
        }

        g_kit_pass[KIT_IDX_MOTOR] = ok_all;
        g_kit_dir_ok = ok_all;
    }

    /* ---------------- ⑥ 电池电压 ---------------- */
    mv = Kit_BatteryMv();
    Kit_Print("[6/6] 电池电压: "); Kit_PrintU32(mv); Kit_Print(" mV\r\n");
    g_kit_pass[KIT_IDX_BATTERY] = ((mv >= KIT_BAT_MIN_MV) && (mv <= KIT_BAT_MAX_MV)) ? 1u : 0u;

    /* ---------------- 汇总 ---------------- */
    n = 0u;
    for (i = 0u; i < 6u; i++)
    {
        if (g_kit_pass[i] != 0u) { n++; }
    }
    g_kit_pass_cnt = n;

    Kit_Print("========= 自检结果: "); Kit_PrintU32(n); Kit_Print("/6 通过 =========\r\n");
    Kit_Print("  [1]LED [2]按键 [3]I2C [4]编码器 [5]电机 [6]电池\r\n  ");
    for (i = 0u; i < 6u; i++)
    {
        Kit_Print((g_kit_pass[i] != 0u) ? " O " : " X ");
    }
    Kit_Print("\r\n(把 g_kit_pass[] 与串口输出一起截图存档 → docs/media/)\r\n");

    return n;
}