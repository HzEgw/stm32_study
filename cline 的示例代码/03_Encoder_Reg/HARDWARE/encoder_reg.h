/**
 ******************************************************************************
 * @file    encoder_reg.h
 * @brief   编码器接口驱动 —— 纯寄存器版 (TIM3 硬件四倍频 + 定时采样)
 *
 *  与 03_Encoder(标准库版) 功能一致: PA6/PA7 接编码器 A/B 相,
 *  PB6 输出与转速成正比的 PWM。
 *
 * ============================ 寄存器速查表 ============================
 *  寄存器        作用            本文件用到的位
 *  ------------  --------------  ------------------------------------------
 *  RCC->APB2ENR  时钟            bit0 AFIOEN, bit2 IOPAEN, bit3 IOPBEN
 *  RCC->APB1ENR  时钟            bit1 TIM3EN, bit2 TIM4EN
 *  GPIOA->CRL    引脚配置        PA6/PA7: CNF=10 上下拉输入 + MODE=00 -> 0x8
 *  GPIOA->ODR    输出/上下拉选择  ODR 对应位 = 1 表示"上拉"(输入模式下)
 *  TIM3->PSC/ARR 时基            预分频 0(72MHz 计数), ARR=0xFFFF
 *  TIM3->CCMR1   通道 1/2 输入    CC1S[1:0]=01, IC1F[7:4]=滤波
 *                                CC2S[9:8]=01, IC2F[15:12]=滤波
 *  TIM3->CCER    使能/极性        CC1E(bit0), CC2E(bit4), 极性位 CCxP
 *  TIM3->SMCR    从模式           SMS[2:0]=011 编码器模式 3(TI12, 四倍频)
 *  TIM3->CR1     控制             bit0 CEN 启动, bit4 DIR 方向(只读)
 *  TIM3->CNT     计数            ★ 硬件自动 +/- , 就是位置
 *  SysTick->*    1ms 节拍         LOAD/VAL/CTRL
 * =====================================================================
 ******************************************************************************
 */
#ifndef __ENCODER_REG_H
#define __ENCODER_REG_H

#include "stm32f10x.h"

/* ===================== 参数配置 ===================== */
/* ---------------------------------------------------------------------------
 * ★ 真机参考值(鱼香 FishBot 二驱小车, 官方固件默认, 见 成长路线\02_...md):
 *     编码器 11 线(霍尔) + 4 倍频 -> 脉冲比 44 ; 减速比 40.5 ; 轮径 65mm
 *     -> 每输出轴一圈 = 44 × 40.5 = 1782 计数 ; 0.114592 mm/计数
 *   减速比含小数, 精确匹配真机时直接写:  #define ENC_COUNTS_PER_REV 1782u
 * --------------------------------------------------------------------------- */
#define ENC_PPR            500u      /* 编码器单相每转脉冲数 */
#define ENC_GEAR           1u        /* 减速比 */
#define ENC_COUNTS_PER_REV (ENC_PPR * 4u * ENC_GEAR)   /* 四倍频后每转计数 */
#define ENC_IC_FILTER      0x0Fu     /* 输入滤波(机械编码器用最强) */
#define ENC_SAMPLE_MS      50u       /* 转速采样周期(ms) */
#define ENC_PWM_FULL_RPM   300u      /* 转速 -> PWM 满量程(rpm) */
#define ENC_PWM_FREQ_HZ    1000u

/* ===================== 对外接口 ===================== */
void     Encoder_Init(void);
void     Encoder_SpeedOutputInit(void);
void     Encoder_Reset(void);
void     Encoder_Update(void);
void     Encoder_SysTickHandler(void);

int32_t  Encoder_GetPosition(void);
int32_t  Encoder_GetDelta(void);
int32_t  Encoder_GetRpm(void);
uint8_t  Encoder_GetDirection(void);
uint32_t Encoder_GetSamples(void);
uint16_t Encoder_GetPwmDuty(void);

#endif /* __ENCODER_REG_H */
