/**
 ******************************************************************************
 * @file    kit_check.h
 * @brief   套件到货验收工具 —— 对外接口
 *
 *  为什么要专门写一个"验收工程":
 *    套件到手时, 最怕"以为是软件问题, 其实是硬件坏的"。这个工程把
 *    **LED / 按键 / 编码器 / 电机(含转向) / IMU / 电池电压** 逐项过一遍,
 *    每项给出 通过/不通过 的结论(Watch 变量 + 串口打印), 30 分钟就能分辨
 *    "板子/电机/编码器有没有问题", 避免后面调 PID 时被硬件问题带偏。
 *
 *  风格说明: 本工程用**标准库**写(FWLIB 分组), 与江协科技教程/后续平衡车代码一致;
 *           寄存器版对照见 03_Encoder_Reg / 05_I2C_MPU6050_Reg。
 ******************************************************************************
 */
#ifndef __KIT_CHECK_H
#define __KIT_CHECK_H

#include "stm32f10x.h"

/* ===================== 自检结果(Keil Watch 直接看) ===================== */
extern volatile uint32_t g_kit_tick;         /* SysTick 1ms 计数 */
extern volatile uint8_t  g_kit_pass[8];      /* 每一项: 1 = 通过, 0 = 未过 */
extern volatile uint8_t  g_kit_pass_cnt;     /* 通过项数 */
extern volatile uint8_t  g_kit_scan_cnt;     /* I2C 扫描到的器件个数 */
extern volatile uint8_t  g_kit_scan_list[8]; /* 扫描到的地址(7 位) */
extern volatile int32_t  g_kit_enc_total[2]; /* 两个编码器的累计计数 */
extern volatile int32_t  g_kit_enc_delta[2]; /* 最近一个采样周期的增量 */
extern volatile int32_t  g_kit_rpm_x100[2];  /* 转速 ×100(整数 Watch 友好) */
extern volatile uint16_t g_kit_bat_mv;       /* 电池电压(mV) */
extern volatile uint8_t  g_kit_dir_ok;       /* 电机转向与编码器方向是否一致 */

/* 自检项编号(对应 g_kit_pass 下标) */
#define KIT_IDX_LED      0u
#define KIT_IDX_KEY      1u
#define KIT_IDX_I2C      2u
#define KIT_IDX_ENCODER  3u
#define KIT_IDX_MOTOR    4u
#define KIT_IDX_BATTERY  5u

/* ===================== 基础外设 ===================== */
void     Kit_Init(void);                       /* 一次把所有外设初始化好 */
void     Kit_LedSet(uint8_t on);
uint8_t  Kit_KeyPressed(void);                 /* 1 = 按下 */
void     Kit_DelayMs(uint32_t ms);             /* 用 SysTick 节拍延时(不占 CPU 空转) */

/* ===================== 电机 / 编码器 ===================== */
void     Kit_MotorSet(uint8_t id, int16_t duty);   /* id: 0/1 duty: -1000~+1000 */
int32_t  Kit_EncoderGet(uint8_t id);               /* 累计计数(带正负) */
int32_t  Kit_EncoderDelta(uint8_t id);             /* 最近一次采样的增量 */
void     Kit_EncoderUpdate(void);                  /* 每 KIT_ENC_SAMPLE_MS 调一次 */
int32_t  Kit_EncoderRpmX100(uint8_t id);           /* 转速 ×100(避免浮点也可看) */

/* ===================== 传感器 / 总线 ===================== */
uint16_t Kit_BatteryMv(void);                      /* 电池电压(mV) */
uint8_t  Kit_I2CScan(uint8_t *list, uint8_t max);   /* I2C2 地址扫描 */
uint8_t  Kit_ImuPresent(void);                     /* 1 = 0x68/0x69 有器件 */

/* ===================== 串口打印(轮询发送, 只发不收) ===================== */
void     Kit_UartInit(void);
void     Kit_Print(const char *s);
void     Kit_PrintU32(uint32_t v);
void     Kit_PrintI32(int32_t v);

/* ===================== 一键自检 ===================== */
uint8_t  Kit_SelfTest(void);                   /* 跑完全部项目, 返回通过项数 */

#endif /* __KIT_CHECK_H */
