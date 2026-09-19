/**
 ******************************************************************************
 * @file    mpu6050_reg.c
 * @brief   I2C1 + MPU6050 驱动实现（纯寄存器版）
 *
 *    ① I2C 底层(直接写寄存器 + 全程超时)   ② MPU6050 器件层
 *    ③ 姿态解算(互补滤波)                 ④ PWM 输出角度(TIM3 寄存器)
 *
 *  ★ 与标准库版的逐行对应(建议左右分屏对照):
 *      标准库                                  本文件
 *      -----------------------------------     ---------------------------------------
 *      I2C_Init(I2C1, &s)                      i2c_hw_init(): CR2 / CCR / TRISE / CR1
 *      I2C_Cmd(I2C1, ENABLE)                   I2C1->CR1 |= PE
 *      I2C_GenerateSTART/STOP                  I2C1->CR1 |= START / STOP
 *      I2C_Send7bitAddress(I2C1, dev, dir)     I2C1->DR = (addr7 << 1) | dir
 *      I2C_CheckEvent(EV5/EV6/EV7/EV8/EV8_2)   i2c_wait_sr1(位掩码) + i2c_clear_addr()
 *      I2C_SendData / I2C_ReceiveData          I2C1->DR 写 / 读
 *      I2C_AcknowledgeConfig(DISABLE)          I2C1->CR1 &= ~ACK
 *      TIM_TimeBaseInit / TIM_OC3Init          TIM3->PSC/ARR/CCMR2/CCER/CR1/EGR
 *      TIM_SetCompare3(TIM3, ccr)              TIM3->CCR3 = ccr
 *
 *  ⚠️ 全程带超时 —— I2C 总线一挂, 没有超时的程序会**死等**(新手最常踩的坑)。
 ****************************************************************************** */

#include "mpu6050_reg.h"
#include <math.h>          /* atan2f / sqrtf；Keil 链接报错时见 README 的替代方案 */

/* ============================ 寄存器位定义(为可读性) ============================ */
#define I2C_CR1_PE      (1u << 0)
#define I2C_CR1_START   (1u << 8)     /* ★ START 在 bit8 */
#define I2C_CR1_STOP    (1u << 9)     /* ★ STOP  在 bit9 */
#define I2C_CR1_ACK     (1u << 10)
#define I2C_CR1_SWRST   (1u << 15)

#define I2C_SR1_SB      (1u << 0)     /* EV5   起始条件已发出 */
#define I2C_SR1_ADDR    (1u << 1)     /* EV6   地址已发送并被应答 */
#define I2C_SR1_BTF     (1u << 2)     /* EV8_2 字节传输完成 */
#define I2C_SR1_RXNE    (1u << 6)     /* EV7   收到一个字节 */
#define I2C_SR1_TXE     (1u << 7)     /* EV8   数据寄存器空 */
#define I2C_SR1_AF      (1u << 10)    /* 无应答(错误) */

#define I2C_SR2_BUSY    (1u << 1)     /* ★ BUSY 是 SR2 的 bit1(不是 bit6) */

/* ---- CCR / TRISE 由目标速率自动选择: 72MHz 系统时 PCLK1(APB1) = 36MHz ---- */
#define I2C_PCLK1_MHZ   36u
#if (I2C_REG_CLOCK_HZ <= 100000u)
  #define I2C_CCR_VAL    180u     /* 标准模式: 36e6 / (2 × 100k) = 180, DUTY 位不用 */
  #define I2C_TRISE_VAL  37u      /* 1000ns / 27.8ns + 1 = 36 + 1 = 37 */
#else
  #define I2C_CCR_VAL    30u      /* 快速模式 2:1: 36e6 / (3 × 400k) = 30 (DUTY=0) */
  #define I2C_TRISE_VAL  12u      /* 300ns / 27.8ns + 1 = 11.8 → 取 12 */
#endif

/* ============================ 私有变量 ============================ */
static float    s_pitch   = 0.0f;
static float    s_roll    = 0.0f;
static uint32_t s_i2c_err = 0u;
static uint8_t  s_inited  = 0u;

/* ============================ 私有函数声明 ============================ */
static void     i2c_hw_init(void);
static uint8_t  i2c_wait_sr1(uint16_t mask);
static uint8_t  i2c_wait_idle(void);
static void     i2c_abort(void);
static void     i2c_clear_addr(void);
static uint8_t  i2c_write_reg(uint8_t addr7, uint8_t reg, uint8_t val);
static uint8_t  i2c_read_regs(uint8_t addr7, uint8_t reg, uint8_t *buf, uint8_t len);
static uint8_t  i2c_probe(uint8_t addr7);

/* ============================ ① I2C 底层 ============================ */
/**
 * @brief  等 I2C1->SR1 的 mask 位**全部为 1**；超时则记错误并释放总线
 * @retval 0 = 等到, 1 = 超时
 * @note   EV5 等 SB / EV6 等 ADDR / EV7 等 RXNE / EV8 等 TXE / EV8_2 等 TXE|BTF
 */
static uint8_t i2c_wait_sr1(uint16_t mask)
{
    uint32_t t = I2C_TIMEOUT;

    while (((I2C1->SR1 & mask) != mask) && (t != 0u))
    {
        t--;
    }
    if (t == 0u)
    {
        s_i2c_err++;
        i2c_abort();                       /* 超时也要发 STOP, 否则总线一直挂着 */
        return 1u;
    }
    return 0u;
}

/**
 * @brief  等总线空闲: SR2 的 BUSY 位为 0
 */
static uint8_t i2c_wait_idle(void)
{
    uint32_t t = I2C_TIMEOUT;

    while (((I2C1->SR2 & I2C_SR2_BUSY) != 0u) && (t != 0u))
    {
        t--;
    }
    if (t == 0u)
    {
        s_i2c_err++;
        return 1u;
    }
    return 0u;
}

/**
 * @brief  出错收尾: 清 AF 标志 + 发 STOP 释放总线
 * @note  AF 是 rc_w0 位 → 向该位写 0 才能清(其余位写 1 无影响)
 */
static void i2c_abort(void)
{
    I2C1->SR1 = (uint16_t)(~I2C_SR1_AF);
    I2C1->CR1 |= I2C_CR1_STOP;
}

/**
 * @brief  清 ADDR 标志: 手册规定"先读 SR1, 再读 SR2"
 */
static void i2c_clear_addr(void)
{
    uint32_t tmp;

    tmp = I2C1->SR1;
    tmp = I2C1->SR2;
    (void)tmp;
}

/**
 * @brief  I2C1 硬件初始化(全寄存器): 时钟 → 引脚 → 软复位 → 时序 → 使能
 */
static void i2c_hw_init(void)
{
    RCC->APB2ENR |= (1u << 0) | (1u << 3);     /* AFIOEN | IOPBEN */
    RCC->APB1ENR |= (1u << 21);                /* I2C1EN */

    /* PB6/PB7 = 复用开漏 50MHz: CRL 的 bit31:24 → 0xFF */
    GPIOB->CRL &= 0x00FFFFFFu;
    GPIOB->CRL |= 0xFF000000u;

    I2C1->CR1 |= I2C_CR1_SWRST;                /* 软复位, 清掉可能残留的忙碌状态 */
    I2C1->CR1 &= (uint16_t)(~I2C_CR1_SWRST);

    I2C1->CR2   = I2C_PCLK1_MHZ;               /* FREQ = PCLK1(MHz) */
    I2C1->CCR   = I2C_CCR_VAL;                 /* DUTY(bit14)=0 → Tlow/Thigh = 2:1 */
    I2C1->TRISE = I2C_TRISE_VAL;               /* 最大允许 SCL 上升时间 */
    I2C1->OAR1  = 0x4000u;                     /* 主模式用不到; bit14 手册要求保持为 1 */

    I2C1->CR1 |= (I2C_CR1_PE | I2C_CR1_ACK);   /* 使能 + 允许应答 */
}

/**
 * @brief  写一个寄存器: START → 地址+W → 寄存器号 → 数据 → STOP
 */
static uint8_t i2c_write_reg(uint8_t addr7, uint8_t reg, uint8_t val)
{
    if (i2c_wait_idle() != 0u)                     { return 1u; }

    I2C1->CR1 |= I2C_CR1_START;                    /* ① 起始 */
    if (i2c_wait_sr1(I2C_SR1_SB) != 0u)            { return 1u; }   /* EV5 */

    I2C1->DR = (uint16_t)((addr7 << 1) | 0x00u);   /* ② 地址 + 写方向 */
    if (i2c_wait_sr1(I2C_SR1_ADDR) != 0u)          { return 1u; }   /* EV6 */
    i2c_clear_addr();

    I2C1->DR = reg;                                /* ③ 寄存器号 */
    if (i2c_wait_sr1(I2C_SR1_TXE) != 0u)           { return 1u; }   /* EV8 */

    I2C1->DR = val;                                /* ④ 数据 */
    if (i2c_wait_sr1(I2C_SR1_TXE | I2C_SR1_BTF) != 0u) { return 1u; }  /* EV8_2 */

    I2C1->CR1 |= I2C_CR1_STOP;                     /* ⑤ 停止 */
    return 0u;
}

/**
 * @brief  连续读寄存器: 写"要读的地址" → RESTART → 地址+R → 连续读
 * @note   最后 1 字节前必须"先关 ACK 再发 STOP"(POS=0 的经典写法),
 *         否则从机会继续送数据, 读到多余字节
 */
static uint8_t i2c_read_regs(uint8_t addr7, uint8_t reg, uint8_t *buf, uint8_t len)
{
    if ((buf == 0) || (len == 0u))                 { return 1u; }
    if (i2c_wait_idle() != 0u)                     { return 1u; }

    /* --- 阶段 1: 告诉器件要读哪个寄存器 --- */
    I2C1->CR1 |= I2C_CR1_START;
    if (i2c_wait_sr1(I2C_SR1_SB) != 0u)            { return 1u; }

    I2C1->DR = (uint16_t)((addr7 << 1) | 0x00u);
    if (i2c_wait_sr1(I2C_SR1_ADDR) != 0u)          { return 1u; }
    i2c_clear_addr();

    I2C1->DR = reg;
    if (i2c_wait_sr1(I2C_SR1_TXE | I2C_SR1_BTF) != 0u) { return 1u; }  /* EV8_2: 要重启 */

    /* --- 阶段 2: 重复起始(RESTART) + 地址+R --- */
    I2C1->CR1 |= I2C_CR1_START;
    if (i2c_wait_sr1(I2C_SR1_SB) != 0u)            { return 1u; }

    I2C1->DR = (uint16_t)((addr7 << 1) | 0x01u);
    if (i2c_wait_sr1(I2C_SR1_ADDR) != 0u)          { return 1u; }
    i2c_clear_addr();

    /* --- 阶段 3: 连续读 --- */
    while (len != 0u)
    {
        if (len == 1u)
        {
            I2C1->CR1 &= (uint16_t)(~I2C_CR1_ACK); /* 关应答(最后一个字节 NACK) */
            I2C1->CR1 |= I2C_CR1_STOP;             /* 请求停止 */
        }
        if (i2c_wait_sr1(I2C_SR1_RXNE) != 0u)      { return 1u; }   /* EV7 */
        *buf = (uint8_t)I2C1->DR;
        buf++;
        len--;
    }

    (void)I2C1->SR1;                     /* 清 STOPF 第一步: 读 SR1 */
    I2C1->CR1 |= I2C_CR1_ACK;            /* 第二步: 写 CR1, 顺便恢复应答 */
    return 0u;
}

/**
 * @brief  【进阶】探测某个 7 位地址上是否有器件应答(只发地址, 随即 STOP)
 * @retval 0 = 有器件应答, 1 = 没有
 * @note   无应答是**正常现象**, 所以这里不累加 s_i2c_err
 */
static uint8_t i2c_probe(uint8_t addr7)
{
    uint32_t t;

    if (i2c_wait_idle() != 0u) { return 1u; }

    I2C1->CR1 |= I2C_CR1_START;
    t = I2C_TIMEOUT;
    while (((I2C1->SR1 & I2C_SR1_SB) == 0u) && (t != 0u)) { t--; }
    if (t == 0u) { i2c_abort(); return 1u; }

    I2C1->DR = (uint16_t)((addr7 << 1) | 0x00u);

    t = I2C_TIMEOUT;
    while ((t != 0u)
           && ((I2C1->SR1 & I2C_SR1_ADDR) == 0u)
           && ((I2C1->SR1 & I2C_SR1_AF)   == 0u)) { t--; }

    if ((I2C1->SR1 & I2C_SR1_ADDR) != 0u)
    {
        i2c_clear_addr();                /* 有应答: 清 ADDR 后正常结束 */
        I2C1->CR1 |= I2C_CR1_STOP;
        return 0u;
    }

    i2c_abort();                         /* 无应答 / 超时: 清 AF + STOP */
    return 1u;
}

/**
 * @brief  【进阶】扫描 0x08~0x77 的从机地址
 * @retval 有应答的地址个数(结果写进 addr_list, 最多存 max_cnt 个)
 * @note   排查"器件到底在不在"最有效的一招; 正常应看到 0x68(MPU6050),
 *         若车上 OLED 同挂一条总线, 还能看到 0x3C
 */
uint8_t MPU_ScanI2C(uint8_t *addr_list, uint8_t max_cnt)
{
    uint8_t addr;
    uint8_t cnt = 0u;

    for (addr = 0x08u; addr <= 0x77u; addr++)
    {
        if (i2c_probe(addr) == 0u)
        {
            if (cnt < max_cnt) { addr_list[cnt] = addr; }
            cnt++;
        }
    }
    return cnt;
}

/* ============================ ② MPU6050 器件层 ============================ */
/**
 * @brief  初始化 I2C1(寄存器) + MPU6050(唤醒 + 采样率 + 量程 + 低通)
 * @note   与标准库版写入的寄存器**完全一致**(左边是寄存器, 右边是含义)
 */
void MPU_Init(void)
{
    volatile uint32_t d;

    i2c_hw_init();

    i2c_write_reg(MPU6050_ADDR, 0x6Bu, 0x80u);   /* PWR_MGMT_1: 软复位 */
    for (d = 200000u; d != 0u; d--) { }          /* 等复位(简单延时) */
    i2c_write_reg(MPU6050_ADDR, 0x6Bu, 0x01u);   /* 唤醒, 时钟源 = PLL(X 陀螺) */
    i2c_write_reg(MPU6050_ADDR, 0x19u, 0x09u);   /* 采样率 = 1kHz/(1+9) = 100Hz */
    i2c_write_reg(MPU6050_ADDR, 0x1Au, 0x03u);   /* 数字低通 44Hz */
    i2c_write_reg(MPU6050_ADDR, 0x1Bu, 0x08u);   /* 陀螺 ±500°/s → 65.5 LSB/(°/s) */
    i2c_write_reg(MPU6050_ADDR, 0x1Cu, 0x08u);   /* 加速度 ±4g   → 8192 LSB/g */

    s_inited = 1u;
}

/**
 * @brief  读 WHO_AM_I(0x75), 正常返回 0x68 —— 排查"接线/地址对不对"的第一招
 */
uint8_t MPU_GetWhoAmI(void)
{
    uint8_t id = 0u;

    if (i2c_read_regs(MPU6050_ADDR, 0x75u, &id, 1u) != 0u)
    {
        return 0u;
    }
    return id;
}

/**
 * @brief  读 6 轴原始数据
 * @retval 0 = 成功; 1 = I2C 出错
 * @note   0x3B 起连续 14 字节: 加速度(6) + 温度(2) + 陀螺(6), 每量 2 字节、**大端**
 */
uint8_t MPU_ReadRaw(int16_t *ax, int16_t *ay, int16_t *az,
                    int16_t *gx, int16_t *gy, int16_t *gz)
{
    uint8_t buf[14];

    if ((ax == 0) || (ay == 0) || (az == 0) || (gx == 0) || (gy == 0) || (gz == 0))
    {
        return 1u;
    }
    if (i2c_read_regs(MPU6050_ADDR, 0x3Bu, buf, 14u) != 0u)
    {
        return 1u;
    }

    *ax = (int16_t)((buf[0] << 8) | buf[1]);
    *ay = (int16_t)((buf[2] << 8) | buf[3]);
    *az = (int16_t)((buf[4] << 8) | buf[5]);
    /* buf[6..7] = 温度(未使用, 练习里可以读出来: T = raw/340 + 36.53) */
    *gx = (int16_t)((buf[8]  << 8) | buf[9]);
    *gy = (int16_t)((buf[10] << 8) | buf[11]);
    *gz = (int16_t)((buf[12] << 8) | buf[13]);
    return 0u;
}

/* ============================ ③ 姿态解算(互补滤波) ============================ */
/**
 * @brief  加速度算倾角 + 陀螺仪积分, 用互补滤波融合出 pitch / roll
 * @param  dt_s 采样间隔(秒), 例如 5ms 采样 → 0.005f
 *
 *  原理: 加速度计静态准、动态被震动干扰; 陀螺仪动态准、长时间会漂移。
 *        互补滤波:  angle = α × (angle + 陀螺积分) + (1-α) × 加速度倾角   (α = 0.98)
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

    ax = (float)axRaw / MPU_ACCEL_LSB_PER_G;
    ay = (float)ayRaw / MPU_ACCEL_LSB_PER_G;
    az = (float)azRaw / MPU_ACCEL_LSB_PER_G;
    gx = (float)gxRaw / MPU_GYRO_LSB_PER_DPS;
    gy = (float)gyRaw / MPU_GYRO_LSB_PER_DPS;

    pitch_acc = atan2f(-ax, sqrtf((ay * ay) + (az * az))) * 57.29578f;
    roll_acc  = atan2f( ay, az) * 57.29578f;

    s_pitch = 0.98f * (s_pitch + gy * dt_s) + 0.02f * pitch_acc;
    s_roll  = 0.98f * (s_roll  - gx * dt_s) + 0.02f * roll_acc;
}

float    MPU_GetPitch(void)      { return s_pitch;   }
float    MPU_GetRoll(void)       { return s_roll;    }
uint32_t MPU_GetErrorCount(void) { return s_i2c_err; }

/* ============================ ④ 可选: PWM 输出角度(TIM3 寄存器) ============================ */
/**
 * @brief  PB0(TIM3_CH3) 输出 1kHz PWM, 占空比反映俯仰角
 * @note   没有 OLED/串口时, 万用表直流档量 PB0: 水平约 1.65V, 前后倾斜电压变化
 *
 *  与标准库版的对应:
 *      TIM_TimeBaseInit →  TIM3->PSC / ARR / CR1(ARPE)
 *      TIM_OC3Init      →  TIM3->CCMR2(OC3M/OC3PE) + CCER(CC3E) + CCR3
 *      TIM_Cmd          →  TIM3->CR1 |= CEN
 *      TIM_GenerateEvent→  TIM3->EGR |= UG
 */
void MPU_PitchPwmInit(void)
{
    RCC->APB2ENR |= (1u << 3);            /* IOPBEN */
    RCC->APB1ENR |= (1u << 1);            /* TIM3EN */

    /* PB0 = TIM3_CH3 → 复用推挽 50MHz: CRL 的 bit3:0 = 0xB */
    GPIOB->CRL &= (uint32_t)(~0x0000000Fu);
    GPIOB->CRL |=  0x0000000Bu;

    TIM3->PSC  = 71u;                     /* 72MHz/(71+1) = 1MHz 计数 */
    TIM3->ARR  = 999u;                    /* 1MHz/(999+1) = 1kHz */
    TIM3->CCMR2 &= (uint16_t)(~(0x7u << 4));          /* 清 OC3M[2:0](bit6:4) */
    TIM3->CCMR2 |= (uint16_t)((0x6u << 4) | (1u << 3)); /* OC3M=110(PWM模式1) + OC3PE 预装载 */
    TIM3->CCR3  = 500u;                   /* 初始 50% 占空比 */
    TIM3->CCER |= (1u << 8);              /* CC3E = 1 使能 CH3 (CC3P=bit9 保持 0 = 高有效) */
    TIM3->CR1  |= (1u << 7) | (1u << 0);  /* ARPE 自动重装载预装载 + CEN 启动计数 */
    TIM3->EGR  |= (1u << 0);              /* UG: 立即把 PSC/ARR 装载生效 */
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
    TIM3->CCR3 = (uint16_t)duty;
}
