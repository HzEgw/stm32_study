/**
 ******************************************************************************
 * @file    mpu6050.h
 * @brief   I2C1(PB6/PB7) + MPU6050 六轴传感器 + 互补滤波姿态解算（标准库版）
 *
 *  这是 W3 的主线工程：补上 I2C 这块外设空白（平衡车的 IMU 就靠它）。
 *
 *  引脚: PB6 = I2C1_SCL, PB7 = I2C1_SDA  (I2C1 默认映射, 无需重映射)
 *        注意: SDA/SCL 必须上拉(4.7k)；MPU6050 模块板一般自带
 *        PB0 = TIM3_CH3 输出"与俯仰角成正比的 PWM"（可选, 便于万用表/示波器观察）
 *
 * ============================ 寄存器速查表 ============================
 *  I2C1 关键寄存器
 *    I2C1->CR1   bit0 PE 使能, bit8 START, bit9 STOP, bit10 ACK, bit11 POS, bit15 SWRST
 *                ★ START 是 bit8、STOP 是 bit9 (bit6 是 ENGC, 最容易记错!)
 *    I2C1->CR2   bit5:0 FREQ = PCLK1(MHz)
 *    I2C1->CCR   时钟控制: 快速模式 2:1 时 CCR = PCLK1/(3×SCL)
 *    I2C1->TRISE 上升时间: 快速模式 = (300ns × PCLK1) + 1 ≈ 12 @36MHz
 *    I2C1->SR1   事件标志: SB(bit0) ADDR(bit1) BTF(bit2) RXNE(bit6) TXE(bit7) AF(bit10)
 *    I2C1->SR2   读它可清 ADDR; bit0 MSL 主模式, bit1 BUSY 总线忙, bit2 TRA 发送方向
 *                ★ BUSY 是 SR2 的 bit1 (不是 bit6!)
 *    I2C1->DR    数据寄存器(读/写)
 *
 *  MPU6050 常用寄存器
 *    0x75 WHO_AM_I (应为 0x68)      0x6B PWR_MGMT_1 (0x80 复位 / 0x00 唤醒)
 *    0x19 SMPLRT_DIV                0x1A CONFIG(数字低通)
 *    0x1B GYRO_CONFIG (±250/500/1000/2000 dps)
 *    0x1C ACCEL_CONFIG (±2/4/8/16 g)
 *    0x3B~0x40 加速度(6 字节)       0x41~0x42 温度
 *    0x43~0x48 陀螺仪(6 字节)       每个量都是 2 字节大端(高字节在前)
 * =====================================================================
 */
#ifndef __MPU6050_H
#define __MPU6050_H

#include "stm32f10x.h"

/* ===================== 器件参数 ===================== */
#define MPU6050_ADDR       0xD0u      /* 7 位地址 0x68 左移 1 位(标准库用 8 位形式) */
#define MPU6050_WHOAMI_ID  0x68u
#define I2C_TIMEOUT        200000u    /* 事件等待超时计数, 防止死等卡死程序 */

/* 量程换算(与 MPU_Init 里的配置一致) */
#define MPU_ACCEL_LSB_PER_G   8192.0f /* ±4g  -> 8192  LSB/g */
#define MPU_GYRO_LSB_PER_DPS  65.5f   /* ±500 -> 65.5  LSB/(°/s) */

/* ===================== 对外接口 ===================== */
void     MPU_Init(void);                 /* I2C1 初始化 + MPU6050 上电配置 */
uint8_t  MPU_GetWhoAmI(void);            /* 读 0x75, 正常应返回 0x68 */
uint8_t  MPU_ReadRaw(int16_t *ax, int16_t *ay, int16_t *az,
                     int16_t *gx, int16_t *gy, int16_t *gz);   /* 0=成功 */
void     MPU_UpdateAttitude(float dt_s); /* 互补滤波: 需要传采样间隔(秒) */
float    MPU_GetPitch(void);             /* 俯仰角(度) */
float    MPU_GetRoll(void);              /* 横滚角(度) */
uint32_t MPU_GetErrorCount(void);        /* I2C 错误计数(应为 0) */

/* ---- 可选: 用 PWM 把角度"输出"出来(没有 OLED/串口也能用万用表看) ---- */
void     MPU_PitchPwmInit(void);         /* PB0(TIM3_CH3) 输出 1kHz PWM */
void     MPU_PitchPwmOutput(float pitch_deg);   /* -45°~+45° 映射到 0~100% */

#endif /* __MPU6050_H */
