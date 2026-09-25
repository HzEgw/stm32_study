/**
 ******************************************************************************
 * @file    main.c
 * @brief   I2C + MPU6050 姿态实验 —— 寄存器版 (W3 主线工程)
 *
 *  与标准库版 05_I2C_MPU6050 的区别:
 *    ① 驱动层直接操作 I2C1 寄存器(见 mpu6050_reg.c 顶部的寄存器速查表)
 *    ② 这里额外把 CR1/CR2/CCR/TRISE/OAR1/SR1/SR2 拷进 Watch 变量, 便于核对初始化
 *    ③ 内置"开机扫一遍 I2C 地址"(标准库版的练习 5)
 *
 *  ★ 验证顺序(从易到难, 务必按顺序):
 *    ① 看 `g_whoami`: Watch 里**应为 0x68**; `g_scan_cnt` 至少为 1, `g_scan_list[0]`=0x68
 *    ② 倾斜板子: `g_pitch` / `g_roll` 跟着变; 静止时应稳定(漂移小)
 *    ③ (可选)万用表直流档量 **PB0**: 水平约 1.65V, 前后倾斜电压变化
 *       寄存器对照: `g_reg_ccr3` 就是 TIM3->CCR3, 应在 0~999 之间
 *
 *  ★ 采样节拍: SysTick 1ms 累加, 每 5ms 采一次(200Hz), 互补滤波 dt = 0.005s
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "mpu6050_reg.h"

#define SAMPLE_MS  5u          /* 采样周期(ms) */

/* ============ Keil Watch 观察点 ============ */
volatile uint32_t g_tick     = 0;      /* SysTick 1ms 计数 */
volatile uint8_t  g_whoami   = 0;      /* 应为 0x68 */
volatile int16_t  g_ax = 0, g_ay = 0, g_az = 0;   /* 加速度原始值 */
volatile int16_t  g_gx = 0, g_gy = 0, g_gz = 0;   /* 陀螺仪原始值 */
volatile float    g_pitch    = 0.0f;   /* 俯仰角(度) */
volatile float    g_roll     = 0.0f;   /* 横滚角(度) */
volatile uint32_t g_samples  = 0;      /* 采样次数 */
volatile uint32_t g_i2c_err  = 0;      /* I2C 错误计数(应为 0) */
volatile uint32_t g_reg_ccr3 = 0;      /* TIM3->CCR3(角度 PWM 的占空比) */

/* ---- 寄存器快照: 核对"初始化到底写进去了什么" ---- */
volatile uint32_t g_reg_cr1   = 0;     /* 期望 PE(bit0)=1, ACK(bit10)=1 → 0x0401 */
volatile uint32_t g_reg_cr2   = 0;     /* 期望 FREQ(bit5:0)=36 → 0x0024 */
volatile uint32_t g_reg_ccr   = 0;     /* 期望 30 (400kHz, DUTY=0) */
volatile uint32_t g_reg_trise = 0;     /* 期望 12 (快速模式) */
volatile uint32_t g_reg_oar1  = 0;     /* 期望 bit14 = 1 → 0x4000 */
volatile uint32_t g_reg_pe    = 0;     /* I2C1->CR1 的 PE 位单看 */
volatile uint32_t g_reg_sr1   = 0;     /* 空闲时两位应为 0: TXE(bit7) 通常会置 1 */
volatile uint32_t g_reg_sr2   = 0;     /* 空闲时 BUSY(bit1) 必须为 0 */

/* ---- I2C 地址扫描结果 ---- */
volatile uint8_t  g_scan_cnt  = 0;     /* 应答的器件个数(含 MPU6050 应 ≥1) */
volatile uint8_t  g_scan_list[8] = {0};/* 应答的地址(7 位), 期望第 1 个是 0x68 */

int main(void)
{
    uint32_t last = 0;
    uint16_t i;
    uint8_t  scan[8];
    int16_t  ax, ay, az, gx, gy, gz;

    SystemInit();                               /* 72MHz (APB1 = 36MHz, 决定 I2C 的 FREQ) */
    SysTick_Config(SystemCoreClock / 1000u);    /* 1ms 节拍 */

    MPU_Init();                                 /* I2C1 寄存器初始化 + MPU6050 配置 */
    MPU_PitchPwmInit();                         /* PB0(TIM3_CH3) 输出角度 PWM */

    /* 把初始化后的寄存器真值拍下来, 与注释里的"期望值"逐位比对 */
    g_reg_cr1    = I2C1->CR1;
    g_reg_cr2    = I2C1->CR2;
    g_reg_ccr    = I2C1->CCR;
    g_reg_trise  = I2C1->TRISE;
    g_reg_oar1   = I2C1->OAR1;
    g_reg_pe     = I2C1->CR1 & 0x0001u;
    g_reg_sr2    = I2C1->SR2;

    /* ① 器件识别 */
    g_whoami = MPU_GetWhoAmI();

    /* ② 开机扫一遍总线(排查"器件在不在"最有效的一招) */
    g_scan_cnt = MPU_ScanI2C(scan, 8u);
    for (i = 0u; i < 8u; i++)
    {
        g_scan_list[i] = scan[i];
    }

    while (1)
    {
        if ((g_tick - last) >= SAMPLE_MS)
        {
            last = g_tick;

            /* ③ 读原始数据(失败就保留上次值, 不污染滤波) */
            if (MPU_ReadRaw(&ax, &ay, &az, &gx, &gy, &gz) == 0u)
            {
                g_ax = ax; g_ay = ay; g_az = az;
                g_gx = gx; g_gy = gy; g_gz = gz;
            }

            /* ④ 互补滤波更新姿态 */
            MPU_UpdateAttitude((float)SAMPLE_MS / 1000.0f);

            g_pitch   = MPU_GetPitch();
            g_roll    = MPU_GetRoll();
            g_samples++;
            g_i2c_err = MPU_GetErrorCount();

            /* ⑤ 把角度"输出"到 PB0, 便于万用表/示波器观察 */
            MPU_PitchPwmOutput(g_pitch);
            g_reg_ccr3 = TIM3->CCR3;

            /* ⑥ 逮一次空闲态的状态寄存器(排查总线卡死时看这里) */
            g_reg_sr1 = I2C1->SR1;
            g_reg_sr2 = I2C1->SR2;
        }
    }
}