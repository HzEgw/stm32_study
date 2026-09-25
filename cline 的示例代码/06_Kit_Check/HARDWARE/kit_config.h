/**
 ******************************************************************************
 * @file    kit_config.h
 * @brief   套件验收工程的全部"可调参数"（集中配置，其余代码不用动）
 *
 * ⚠️ **到货后第一件事**：拿着套件原理图/板子丝印，把下面这张表**逐行核对并改掉**。
 *    改完这个文件就够 —— 这正是"参数集中配置"的工程习惯（别把引脚写死在逻辑里）。
 *
 * ═════════════════ 默认映射（F103C8T6 核心板 + 双 H 桥 + 2×编码器电机）═════════════════
 *  功能            引脚          外设              说明
 *  ---------------------------------------------------------------------------------
 *  电机1 PWM       PA0           TIM2_CH1          双 PWM 驱动: 另一路拉低
 *  电机1 方向      PA1           GPIO 推挽         (与鱼香官方固件"双 PWM"同思路)
 *  电机2 PWM       PA2           TIM2_CH3
 *  电机2 方向      PA3           GPIO 推挽
 *  编码器1 A/B     PA6 / PA7     TIM3 编码器模式    硬件 4 倍频
 *  编码器2 A/B     PB6 / PB7     TIM4 编码器模式
 *  IMU 探测        PB10 / PB11   I2C2              只做"地址扫描"; 完整驱动在 05 工程
 *  电池电压        PA4           ADC1_IN4          分压后进 ADC
 *  LED             PC13          GPIO(低电平点亮)   板载 LED
 *  按键            PA5           GPIO 上拉输入      按下 = 低
 *  串口打印        PA9 / PA10    USART1            115200 8N1(USB-TTL 接这里)
 *
 *  ⚠️ 注意: 江协科技平衡车主板是**一体化板**(自带 TB6612/MPU6050/OLED),
 *     引脚可能与上表不同 —— 以你板子丝印为准, 改本文件即可。
 *
 *  ⚠️ 再注意: **PWM 脚与编码器脚不能随便换端口** —— 它们由定时器通道硬件决定
 *     (PA0 只能是 TIM2_CH1、PA6/PA7 只能是 TIM3_CH1/CH2 …)。要换脚必须同时换通道,
 *     例如编码器2 改用 TIM1(PA8/PA9) 或 TIM2 重映射。LED/按键/方向脚则可以自由改。
 ******************************************************************************
 */
#ifndef __KIT_CONFIG_H
#define __KIT_CONFIG_H

#include "stm32f10x.h"

/* ======================= ① 引脚映射 ======================= */
/* --- 电机 (双 PWM: 一个脚给占空比, 另一个脚拉低表示方向) --- */
#define KIT_M1_PWM_PORT      GPIOA
#define KIT_M1_PWM_PIN       GPIO_Pin_0      /* TIM2_CH1 */
#define KIT_M1_DIR_PORT      GPIOA
#define KIT_M1_DIR_PIN       GPIO_Pin_1

#define KIT_M2_PWM_PORT      GPIOA
#define KIT_M2_PWM_PIN       GPIO_Pin_2      /* TIM2_CH3 */
#define KIT_M2_DIR_PORT      GPIOA
#define KIT_M2_DIR_PIN       GPIO_Pin_3

/* --- 编码器 (A/B 两相, 用定时器"编码器接口模式") --- */
#define KIT_ENC1_PORT        GPIOA
#define KIT_ENC1_A_PIN       GPIO_Pin_6      /* TIM3_CH1 */
#define KIT_ENC1_B_PIN       GPIO_Pin_7      /* TIM3_CH2 */

#define KIT_ENC2_PORT        GPIOB
#define KIT_ENC2_A_PIN       GPIO_Pin_6      /* TIM4_CH1 */
#define KIT_ENC2_B_PIN       GPIO_Pin_7      /* TIM4_CH2 */

/* --- I2C2 (PB10=SCL / PB11=SDA) 只用来看"IMU 在不在" --- */
#define KIT_I2C_PORT         GPIOB
#define KIT_I2C_SCL_PIN      GPIO_Pin_10
#define KIT_I2C_SDA_PIN      GPIO_Pin_11
#define KIT_IMU_ADDR1        0x68u           /* MPU6050(AD0=GND) */
#define KIT_IMU_ADDR2        0x69u           /* MPU6050(AD0=VCC) */

/* --- 电池电压 ADC: PA4 = ADC1_IN4 --- */
#define KIT_ADC_PORT         GPIOA
#define KIT_ADC_PIN          GPIO_Pin_4
#define KIT_ADC_CHANNEL      ADC_Channel_4

/* --- LED / 按键 --- */
#define KIT_LED_PORT         GPIOC
#define KIT_LED_PIN          GPIO_Pin_13
#define KIT_LED_ACTIVE_LOW   1u              /* 1 = 低电平点亮(蓝靛板载 LED 常见) */
#define KIT_KEY_PORT         GPIOA
#define KIT_KEY_PIN          GPIO_Pin_5

/* ======================= ② 电机 / 编码器 参数 ======================= */
/**
 * 这几个数取自鱼香官方固件默认值(见 成长路线\02_FishBot官方固件参数与STM32移植映射.md)
 *  → 平衡车套件的电机参数**必须自己实测**(套件说明书一般会给"每圈脉冲数")。
 *    实测方法: 手转输出轮一圈, 读 Kit_EncoderGet() 的增量, 就是 KIT_COUNTS_PER_REV。
 */
#define KIT_ENC_PULSE_RATIO    44u           /* 电机转一圈的脉冲数 = 11 线 × 4 倍频 */
#define KIT_ENC_GEAR_RATIO     40.5f         /* 减速比 */
#define KIT_WHEEL_DIAMETER_MM  65.0f         /* 轮径(mm) */
#define KIT_COUNTS_PER_REV     1782.0f       /* 输出轴一圈的计数 = 44 × 40.5 */
#define KIT_WHEEL_CIRC_MM      204.2035f     /* 轮周长 = π × 65 */
#define KIT_ENC_SAMPLE_MS      10u           /* 测速采样周期(ms) */
#define KIT_PWM_FREQ_HZ        20000u        /* PWM 频率: 20kHz 安静; 官方固件用 5kHz(可对比) */
#define KIT_DUTY_MAX           1000          /* 占空比量程: 千分比 -1000~+1000 */

/* ======================= ③ 电池电压换算 ======================= */
#define KIT_ADC_VREF_MV        3300u         /* ADC 参考电压(mV) */
#define KIT_ADC_DIV_NUM        11u           /* 分压系数 = (R上 + R下) / R下 */
#define KIT_ADC_DIV_DEN        1u            /* 例: 100k+10k → 分母 10k → 11/1 */
#define KIT_BAT_MIN_MV         9000u         /* 判定"电池接上了"的下限(12V 系统) */
#define KIT_BAT_MAX_MV         14000u        /* 判定"电压正常"的上限 */

/* ======================= ④ 自检节拍参数 ======================= */
#define KIT_MOTOR_TEST_DUTY    400           /* 电机测试占空比(千分比) */
#define KIT_MOTOR_TEST_MS      1000u         /* 每个方向转多久 */
#define KIT_ENC_HAND_TEST_MS   3000u         /* 手转轮子的观察窗口 */
#define KIT_KEY_WAIT_MS        3000u         /* 等按键按下的时间 */

#endif /* __KIT_CONFIG_H */
