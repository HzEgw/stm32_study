/**
 ******************************************************************************
 * @file    main.c
 * @brief   I2C + MPU6050 姿态实验（W3 主线工程）
 *
 *  ★ 验证顺序（从易到难，务必按顺序）：
 *   ① 看 WHO_AM_I：Watch 里 `g_whoami` **应为 0x68**
 *       不是 0x68 → 接线/供电/地址问题（见 README 常见问题）
 *   ② 把板子倾斜：`g_pitch` / `g_roll` 跟着变化；静止时读数应稳定（漂移小）
 *   ③ （可选）万用表直流档量 **PB0**：占空比随 pitch 变化 → 电压变化
 *       水平约 1.65V，前倾电压降低、后倾电压升高（-45°~+45° 映射 0~100%）
 *   ④ 进阶：把数据从串口发出去画曲线（把 04 工程的 uart.c 加进来即可，README 有说明）
 *
 *  ★ 采样节拍：SysTick 1ms 累加，每 5ms 采一次（200Hz），传给互补滤波 dt=0.005s
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "mpu6050.h"

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
volatile uint32_t g_reg_ccr3 = 0;      /* 直接读 TIM3->CCR3(角度 PWM 的占空比) */
volatile uint32_t g_reg_pe   = 0;      /* I2C1->CR1 的 PE 位(应为 1) */

int main(void)
{
    uint32_t last = 0;
    int16_t  ax, ay, az, gx, gy, gz;

    SystemInit();                               /* 72MHz */
    SysTick_Config(SystemCoreClock / 1000u);    /* 1ms 节拍 */

    MPU_Init();                                 /* I2C1(PB6/PB7) + MPU6050 配置 */
    MPU_PitchPwmInit();                         /* PB0 输出角度指示 PWM(可选) */

    g_whoami = MPU_GetWhoAmI();                 /* ① 应读到 0x68 */
    g_reg_pe = I2C1->CR1 & 0x0001u;             /* PE 使能位快照 */

    while (1)
    {
        if ((g_tick - last) >= SAMPLE_MS)
        {
            last = g_tick;

            /* ② 读原始数据(失败就保留上次值, 不污染滤波) */
            if (MPU_ReadRaw(&ax, &ay, &az, &gx, &gy, &gz) == 0u)
            {
                g_ax = ax; g_ay = ay; g_az = az;
                g_gx = gx; g_gy = gy; g_gz = gz;
            }

            /* ③ 互补滤波更新姿态 */
            MPU_UpdateAttitude((float)SAMPLE_MS / 1000.0f);

            g_pitch   = MPU_GetPitch();
            g_roll    = MPU_GetRoll();
            g_samples++;
            g_i2c_err = MPU_GetErrorCount();

            /* ④ 把角度"输出"到 PB0, 便于万用表/示波器观察 */
            MPU_PitchPwmOutput(g_pitch);
            g_reg_ccr3 = TIM3->CCR3;
        }
    }
}
