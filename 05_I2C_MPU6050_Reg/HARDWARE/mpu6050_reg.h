/**
 ******************************************************************************
 * @file    mpu6050_reg.h
 * @brief   I2C1(PB6/PB7) + MPU6050 六轴姿态 —— 纯寄存器版
 *          （功能与 05_I2C_MPU6050 标准库版**完全相同**）
 *
 *  为什么叫"寄存器版": 初始化与收发**全部直接写 I2C1 的寄存器**,
 *  不调用 I2C_Init / I2C_CheckEvent 等标准库函数；函数签名与标准库版一致,
 *  方便你把两份工程并排放在屏幕上逐行对照。
 *
 *  引脚: PB6 = I2C1_SCL, PB7 = I2C1_SDA (复用开漏 + 上拉, 模块板一般自带 4.7k)
 *        PB0 = TIM3_CH3 输出"与俯仰角成正比"的 1kHz PWM (没有串口/OLED 也能看)
 *
 * ============================ 寄存器速查表 ============================
 *  I2C1->CR1   bit0 PE 使能 | bit8 START | bit9 STOP | bit10 ACK | bit11 POS | bit15 SWRST
 *              ★ 最容易记错: START 在 **bit8**、STOP 在 **bit9**
 *                (bit6 是 ENGC, 不是 START! 见 RM0008 §26.6.1)
 *  I2C1->CR2   bit5:0 FREQ = APB1 频率(MHz) = 36   (72MHz 系统 → APB1 = 36MHz)
 *  I2C1->CCR   bit14 DUTY(0 = 2:1) | bit11:0 CCR = PCLK1/(3×SCL) = 36e6/(3×400k) = 30
 *  I2C1->TRISE bit5:0 = (300ns / T_PCLK1) + 1 ≈ 36×0.3 + 1 = 11.8 → 取 **12**
 *  I2C1->DR    数据寄存器 (写 = 发送, 读 = 接收)
 *  I2C1->SR1   bit0 SB | bit1 ADDR | bit2 BTF | bit6 RXNE | bit7 TXE | bit9 ARLO | bit10 AF
 *  I2C1->SR2   bit0 MSL | bit1 BUSY | bit2 TRA
 *              ★ BUSY 是 SR2 的 **bit1** (不是 bit6!); 清 ADDR 的方法 = 先读 SR1 再读 SR2
 *              出错(AF)后: 写 0 到 AF 位清除 + 发 STOP 释放总线
 *
 *  RCC->APB1ENR  bit21 I2C1EN          RCC->APB2ENR  bit0 AFIOEN | bit3 IOPBEN
 *  GPIOB->CRL    PB6 = bit27:24, PB7 = bit31:28 → 都写 0xF
 *                [CNF=11 复用开漏][MODE=11 50MHz] → 一行: GPIOB->CRL |= 0xFF000000
 *
 *  TIM3 (PB0 输出角度 PWM)
 *    TIM3->PSC = 71 (1MHz 计数) | ARR = 999 (1kHz)
 *    TIM3->CCMR2: OC3M = 110 (bit6:4, PWM 模式1) + OC3PE = 1 (bit3)
 *    TIM3->CCER : CC3E = bit8 (使能 CH3) | CC3P = bit9 (极性)
 *    TIM3->CR1  : ARPE = bit7 | CEN = bit0     TIM3->EGR : UG = bit0
 *    TIM3->CCR3 : 占空比(0~1000)
 * =====================================================================
 */
#ifndef __MPU6050_REG_H
#define __MPU6050_REG_H

#include "stm32f10x.h"

/* ===================== 器件参数 ===================== */
#define MPU6050_ADDR       0x68u      /* 7 位地址(寄存器版用 7 位! 发送时自动 <<1) */
#define MPU6050_ADDR_8BIT  0xD0u      /* 8 位形式, 与标准库版对照用 */
#define MPU6050_WHOAMI_ID  0x68u
#define I2C_TIMEOUT        200000u    /* 事件等待超时计数, 防止总线挂死时程序死等 */

/* ---- I2C 速率: 改这里, 单位 Hz（内部按 400k / 100k 两种自动选 CCR/TRISE） ---- */
#define I2C_REG_CLOCK_HZ   400000u

/* 量程换算(与 MPU_Init 里的配置一致) */
#define MPU_ACCEL_LSB_PER_G   8192.0f /* ±4g  → 8192  LSB/g */
#define MPU_GYRO_LSB_PER_DPS  65.5f   /* ±500 → 65.5  LSB/(°/s) */

/* ===================== 对外接口(与标准库版同名同签名) ===================== */
void     MPU_Init(void);                 /* I2C1 寄存器初始化 + MPU6050 上电配置 */
uint8_t  MPU_GetWhoAmI(void);            /* 读 0x75, 正常应返回 0x68 */
uint8_t  MPU_ReadRaw(int16_t *ax, int16_t *ay, int16_t *az,
                     int16_t *gx, int16_t *gy, int16_t *gz);   /* 0 = 成功 */
void     MPU_UpdateAttitude(float dt_s); /* 互补滤波: 需要传采样间隔(秒) */
float    MPU_GetPitch(void);             /* 俯仰角(度) */
float    MPU_GetRoll(void);              /* 横滚角(度) */
uint32_t MPU_GetErrorCount(void);        /* I2C 错误计数(应为 0) */

/* ---- 可选: 用 PWM 把角度"输出"出来(没有 OLED/串口也能用万用表看) ---- */
void     MPU_PitchPwmInit(void);         /* PB0(TIM3_CH3) 输出 1kHz PWM */
void     MPU_PitchPwmOutput(float pitch_deg);   /* -45°~+45° 映射到 0~100% */

/* ---- 【进阶】I2C 从机地址扫描: 返回应答的地址个数, 结果写进 addr_list ---- */
uint8_t  MPU_ScanI2C(uint8_t *addr_list, uint8_t max_cnt);

#endif /* __MPU6050_REG_H */
