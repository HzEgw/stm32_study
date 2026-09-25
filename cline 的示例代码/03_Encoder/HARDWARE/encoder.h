/**
 ******************************************************************************
 * @file    encoder.h
 * @brief   编码器接口驱动 —— TIM3 硬件四倍频计数 + 定时采样算转速/位置
 *
 *  为什么用"编码器接口模式"而不是外部中断?
 *    硬件自动对 A/B 两相的**双边沿**计数(四倍频)并自动判方向, CPU 完全不参与,
 *    这就是工业上最常用的方案; 中断法在高速时会丢脉冲。
 *
 *  引脚:
 *    PA6 = TIM3_CH1 (编码器 A 相)      PA7 = TIM3_CH2 (编码器 B 相)
 *    PB6 = TIM4_CH1 (输出: 与转速成正比的 PWM, 万用表就能看转速)
 *
 *  计数精度: 4 倍频 -> 每转计数 = PPR × 4 × 减速比
 *  转速公式: rpm = (采样周期内的计数增量 / 每转计数) × (60000 / 采样周期ms)
 *
 *  没有编码器也能验证: 把 PA6 或 PA7 用杜邦线碰一下 GND(或 3.3V) 产生边沿,
 *  位置计数会 +/- 变化; 或者用信号发生器往 PA6/PA7 打两路 90° 相差方波。
 ******************************************************************************
 */
#ifndef __ENCODER_H
#define __ENCODER_H

#include "stm32f10x.h"

/* ===================== 参数配置(按你的编码器改这里) ===================== */
/* ---------------------------------------------------------------------------
 * ★ 真机参考值(鱼香 FishBot 二驱小车, 官方固件默认, 见 成长路线\02_...md):
 *     编码器 11 线(霍尔) + 4 倍频 -> 脉冲比 44 ; 减速比 40.5 ; 轮径 65mm
 *     -> 每输出轴一圈 = 44 × 40.5 = 1782 计数 ; 0.114592 mm/计数
 *   注意减速比 40.5 含小数, 上面的宏是整数运算, 精确匹配真机时请直接写:
 *     #define ENC_COUNTS_PER_REV  1782u
 *   下面保留通用值(500 线直连)以便接其他编码器时不用改结构。
 * --------------------------------------------------------------------------- */
#define ENC_PPR            500u      /* 编码器单相每转脉冲数(线数) */
#define ENC_GEAR           1u        /* 减速比: 直连=1, 1:30 减速箱=30 */
#define ENC_COUNTS_PER_REV (ENC_PPR * 4u * ENC_GEAR)   /* 四倍频后每转计数 */
#define ENC_IC_FILTER      0x0Fu     /* 输入滤波(机械编码器建议 0x0F 最强滤波) */
#define ENC_SAMPLE_MS      50u       /* 转速采样周期(ms) */

/* 转速 -> PWM 输出的满量程(rpm): 达到这个转速时 PWM 输出 100% */
#define ENC_PWM_FULL_RPM   300u
#define ENC_PWM_FREQ_HZ    1000u

/* ===================== 对外接口 ===================== */
void     Encoder_Init(void);              /* TIM3 编码器接口 + SysTick 1ms 采样节拍 */
void     Encoder_SpeedOutputInit(void);   /* PB6 输出与转速成正比的 PWM */
void     Encoder_Reset(void);             /* 位置与转速清零 */

int32_t  Encoder_GetPosition(void);       /* 累计位置(四倍频计数, 带正负) */
int32_t  Encoder_GetDelta(void);          /* 最近一个采样周期的计数增量 */
int32_t  Encoder_GetRpm(void);            /* 转速(rpm, 带符号) */
uint8_t  Encoder_GetDirection(void);      /* 0 = 正转, 1 = 反转 (来自 CR1 的 DIR 位) */
uint32_t Encoder_GetSamples(void);        /* 已完成的采样次数 */
uint16_t Encoder_GetPwmDuty(void);        /* 当前送给 PB6 的占空比(千分比) */

/* 每完成一次采样会被调用: 由主循环轮询(见 Encoder_Update) */
void     Encoder_Update(void);
void     Encoder_SysTickHandler(void);    /* 供 stm32f10x_it.c 调用 */

#endif /* __ENCODER_H */
