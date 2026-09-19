/**
 ******************************************************************************
 * @file    mpu6050.c
 * @brief   I2C1 + MPU6050 驱动实现（标准库版）
 *
 *    ① I2C 底层(带超时)  ② MPU6050 器件层  ③ 姿态解算(互补滤波)  ④ PWM 输出角度
 *
 *  注意: **所有等待事件都带超时** —— I2C 总线一挂, 没有超时的程序就死等(新手最常见坑)。
 ******************************************************************************
 */

#include "mpu6050.h"
#include <math.h>          /* atan2f / sqrtf；Keil 报链接错误时见 README 的替代方案 */

/* ============================ 私有变量 ============================ */
static float    s_pitch   = 0.0f;
static float    s_roll    = 0.0f;
static uint32_t s_i2c_err = 0u;
static uint8_t  s_inited  = 0u;

/* ============================ 私有函数声明 ============================ */
static uint8_t I2C_WriteReg(uint8_t dev, uint8_t reg, uint8_t val);
static uint8_t I2C_ReadRegs(uint8_t dev, uint8_t reg, uint8_t *buf, uint8_t len);

/* 等待事件(带超时): 超时则计数并返回 1 */
#define I2C_WAIT_EVENT(ev)                                                 \
    do {                                                                   \
        uint32_t t_ = I2C_TIMEOUT;                                         \
        while ((I2C_CheckEvent(I2C1, (ev)) == RESET) && (--t_ != 0u)) { }  \
        if (t_ == 0u) { s_i2c_err++; return 1u; }                          \
    } while (0)

/* ============================ ① I2C 底层 ============================ */
/**
 * @brief  写寄存器: START -> 地址+W -> 寄存器号 -> 数据 -> STOP
 */
static uint8_t I2C_WriteReg(uint8_t dev, uint8_t reg, uint8_t val)
{
    I2C_WAIT_EVENT(I2C_EVENT_MASTER_BUSY);        /* 等总线空闲(读 SR2 的 BUSY) */

    I2C_GenerateSTART(I2C1, ENABLE);              /* -> CR1 |= (1<<8)  START */
    I2C_WAIT_EVENT(I2C_EVENT_MASTER_MODE_SELECT); /* EV5: SR1 的 SB(bit0) 置位 */

    I2C_Send7bitAddress(I2C1, dev, I2C_Direction_Transmitter);
    I2C_WAIT_EVENT(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED);  /* EV6: ADDR 已清 */

    I2C_SendData(I2C1, reg);                      /* 写寄存器号到 DR */
    I2C_WAIT_EVENT(I2C_EVENT_MASTER_BYTE_TRANSMITTING);          /* EV8: TXE=1 */

    I2C_SendData(I2C1, val);                      /* 写数据到 DR */
    I2C_WAIT_EVENT(I2C_EVENT_MASTER_BYTE_TRANSMITTED);           /* EV8_2: BTF=1 */

    I2C_GenerateSTOP(I2C1, ENABLE);               /* -> CR1 |= (1<<9)  STOP */
    return 0u;
}

/**
 * @brief  连续读寄存器: 写"要读的地址" -> 重启 -> 地址+R -> 连续读
 * @note   最后 1 字节前必须先关应答再发 STOP, 否则会多读一个字节
 */
static uint8_t I2C_ReadRegs(uint8_t dev, uint8_t reg, uint8_t *buf, uint8_t len)
{
    if ((buf == 0) || (len == 0u))
    {
        return 1u;
    }

    I2C_WAIT_EVENT(I2C_EVENT_MASTER_BUSY);

    /* --- 阶段 1: 告诉器件要读哪个寄存器 --- */
    I2C_GenerateSTART(I2C1, ENABLE);
    I2C_WAIT_EVENT(I2C_EVENT_MASTER_MODE_SELECT);
    I2C_Send7bitAddress(I2C1, dev, I2C_Direction_Transmitter);
    I2C_WAIT_EVENT(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED);
    I2C_SendData(I2C1, reg);
    I2C_WAIT_EVENT(I2C_EVENT_MASTER_BYTE_TRANSMITTED);

    /* --- 阶段 2: 重启(不发 STOP) + 读 --- */
    I2C_GenerateSTART(I2C1, ENABLE);
    I2C_WAIT_EVENT(I2C_EVENT_MASTER_MODE_SELECT);
    I2C_Send7bitAddress(I2C1, dev, I2C_Direction_Receiver);
    I2C_WAIT_EVENT(I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED);     /* EV6 */

    while (len > 0u)
    {
        if (len == 1u)
        {
            I2C_AcknowledgeConfig(I2C1, DISABLE);                 /* -> CR1 清 ACK(bit10) */
            I2C_GenerateSTOP(I2C1, ENABLE);
        }
        I2C_WAIT_EVENT(I2C_EVENT_MASTER_BYTE_RECEIVED);           /* EV7: RXNE=1 */
        *buf = I2C_ReceiveData(I2C1);                             /* 读 DR */
        buf++;
        len--;
    }

    I2C_AcknowledgeConfig(I2C1, ENABLE);
    return 0u;
}

/* ============================ ② MPU6050 器件层 ============================ */
/**
 * @brief  初始化 I2C1(400kHz) 与 MPU6050(唤醒 + 采样率 + 量程 + 低通)
 */
void MPU_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    I2C_InitTypeDef  I2C_InitStructure;
    volatile uint32_t d;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
    /* ↑ 寄存器: RCC->APB2ENR |= (1<<3)|(1<<0);   RCC->APB1ENR |= (1<<21) I2C1EN */

    /* PB6/PB7 必须"复用开漏" + 外部上拉(模块一般自带 4.7k) */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_OD;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    /* ↑ 寄存器: GPIOB->CRL 的 bit31:24 = 0xFF  ([CNF=11 复用开漏][MODE=11 50MHz]) */

    I2C_InitStructure.I2C_Mode                = I2C_Mode_I2C;
    I2C_InitStructure.I2C_DutyCycle           = I2C_DutyCycle_2;   /* -> CCR 的 DUTY(bit14)=0 */
    I2C_InitStructure.I2C_OwnAddress1         = 0x00u;
    I2C_InitStructure.I2C_Ack                 = I2C_Ack_Enable;    /* -> CR1 的 ACK(bit10)=1 */
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_InitStructure.I2C_ClockSpeed          = 400000u;           /* -> CCR / TRISE */
    I2C_Init(I2C1, &I2C_InitStructure);
    /* ↑ I2C_Init 内部: CR2 的 FREQ = PCLK1(MHz); CCR = PCLK1/(2×400k); TRISE = PCLK1+1 */

    I2C_Cmd(I2C1, ENABLE);                       /* -> CR1 |= (1<<0)  PE 使能 */
    I2C_AcknowledgeConfig(I2C1, ENABLE);

    I2C_WriteReg(MPU6050_ADDR, 0x6Bu, 0x80u);    /* PWR_MGMT_1: 软复位 */
    for (d = 200000u; d != 0u; d--) { }          /* 等复位(简单延时) */
    I2C_WriteReg(MPU6050_ADDR, 0x6Bu, 0x01u);    /* 唤醒, 时钟源 = PLL(X 陀螺) */
    I2C_WriteReg(MPU6050_ADDR, 0x19u, 0x09u);    /* 采样率 = 1kHz/(1+9) = 100Hz */
    I2C_WriteReg(MPU6050_ADDR, 0x1Au, 0x03u);    /* 数字低通 44Hz */
    I2C_WriteReg(MPU6050_ADDR, 0x1Bu, 0x08u);    /* 陀螺 ±500°/s (65.5 LSB/(°/s)) */
    I2C_WriteReg(MPU6050_ADDR, 0x1Cu, 0x08u);    /* 加速度 ±4g   (8192 LSB/g) */

    s_inited = 1u;
}

/**
 * @brief  读 WHO_AM_I(0x75), 正常返回 0x68 —— 排查"接线/地址对不对"的第一招
 */
uint8_t MPU_GetWhoAmI(void)
{
    uint8_t id = 0u;
    if (I2C_ReadRegs(MPU6050_ADDR, 0x75u, &id, 1u) != 0u)
    {
        return 0u;
    }
    return id;
}

/**
 * @brief  读 6 轴原始数据
 * @retval 0 = 成功; 1 = I2C 出错
 * @note   0x3B 起连续 14 字节: 加速度(6) + 温度(2) + 陀螺(6), 每量 2 字节、大端
 */
uint8_t MPU_ReadRaw(int16_t *ax, int16_t *ay, int16_t *az,
                    int16_t *gx, int16_t *gy, int16_t *gz)
{
    uint8_t buf[14];

    if ((ax == 0) || (ay == 0) || (az == 0) || (gx == 0) || (gy == 0) || (gz == 0))
    {
        return 1u;
    }
    if (I2C_ReadRegs(MPU6050_ADDR, 0x3Bu, buf, 14u) != 0u)
    {
        return 1u;
    }

    *ax = (int16_t)((buf[0] << 8) | buf[1]);
    *ay = (int16_t)((buf[2] << 8) | buf[3]);
    *az = (int16_t)((buf[4] << 8) | buf[5]);
    /* buf[6..7] = 温度(未使用) */
    *gx = (int16_t)((buf[8]  << 8) | buf[9]);
    *gy = (int16_t)((buf[10] << 8) | buf[11]);
    *gz = (int16_t)((buf[12] << 8) | buf[13]);
    return 0u;
}

/* ============================ ③ 姿态解算(互补滤波) ============================ */
/**
 * @brief  加速度算倾角 + 陀螺仪积分, 用互补滤波融合出 pitch / roll
 * @param  dt_s 采样间隔(秒), 例如 5ms 采样 -> 0.005f
 *
 *  为什么这样做: 加速度计静态准、动态被震动干扰; 陀螺仪动态准、长时间会漂移。
 *  互补滤波:  angle = α × (angle + 陀螺积分) + (1-α) × 加速度倾角        (α 取 0.98)
 */
void MPU_UpdateAttitude(float dt_s)
{
    int16_t axRaw, ayRaw, azRaw, gxRaw, gyRaw, gzRaw;
    float   ax, ay, az, gx, gy;
    float   pitch_acc, roll_acc;

    if (s_inited == 0u)
    {
        return;
    }
    if (MPU_ReadRaw(&axRaw, &ayRaw, &azRaw, &gxRaw, &gyRaw, &gzRaw) != 0u)
    {
        return;                       /* 读失败保持上次角度, 不污染滤波 */
    }

    /* 原始值 -> 物理量 */
    ax = (float)axRaw / MPU_ACCEL_LSB_PER_G;
    ay = (float)ayRaw / MPU_ACCEL_LSB_PER_G;
    az = (float)azRaw / MPU_ACCEL_LSB_PER_G;
    gx = (float)gxRaw / MPU_GYRO_LSB_PER_DPS;
    gy = (float)gyRaw / MPU_GYRO_LSB_PER_DPS;

    /* 加速度倾角(度): 用 atan2f 避免除零; sqrtf 算水平分量模长 */
    pitch_acc = atan2f(-ax, sqrtf((ay * ay) + (az * az))) * 57.29578f;
    roll_acc  = atan2f( ay, az) * 57.29578f;

    /* 互补滤波 */
    s_pitch = 0.98f * (s_pitch + gy * dt_s) + 0.02f * pitch_acc;
    s_roll  = 0.98f * (s_roll  - gx * dt_s) + 0.02f * roll_acc;
}

float MPU_GetPitch(void)         { return s_pitch;   }
float MPU_GetRoll(void)          { return s_roll;    }
uint32_t MPU_GetErrorCount(void) { return s_i2c_err; }

/* ============================ ④ 可选: PWM 输出角度 ============================ */
/**
 * @brief  PB0(TIM3_CH3) 输出 1kHz PWM, 占空比反映俯仰角
 * @note   没有 OLED/串口时, 万用表直流档量 PB0: 水平约 1.65V, 前后倾斜电压变化
 */
void MPU_PitchPwmInit(void)
{
    GPIO_InitTypeDef        GPIO_InitStructure;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_OCInitTypeDef       TIM_OCInitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_0;          /* TIM3_CH3 = PB0 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    TIM_TimeBaseStructure.TIM_Prescaler         = 71u;   /* 1MHz 计数 */
    TIM_TimeBaseStructure.TIM_Period            = 999u;  /* 1kHz, 周期 1000 计数 */
    TIM_TimeBaseStructure.TIM_ClockDivision     = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode       = TIM_CounterMode_Up;
    TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);

    TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse       = 500u;          /* 初始 50% */
    TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_High;
    TIM_OC3Init(TIM3, &TIM_OCInitStructure);
    TIM_OC3PreloadConfig(TIM3, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM3, ENABLE);

    TIM_Cmd(TIM3, ENABLE);
    TIM_GenerateEvent(TIM3, TIM_EventSource_Update);
}

/**
 * @brief  把 -45°~+45° 映射为 0~100% 占空比输出
 */
void MPU_PitchPwmOutput(float pitch_deg)
{
    float duty;

    if (pitch_deg < -45.0f) { pitch_deg = -45.0f; }
    if (pitch_deg >  45.0f) { pitch_deg =  45.0f; }

    duty = (pitch_deg + 45.0f) / 90.0f * 1000.0f;    /* 千分比 0~1000 */
    TIM_SetCompare3(TIM3, (uint16_t)duty);           /* -> TIM3->CCR3 */
}
