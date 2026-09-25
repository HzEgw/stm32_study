/**
 ******************************************************************************
 * @file    LED.h
 * @brief   板上 3 个 LED（PA1/PA2/PA3）的驱动 + 三种考核要求的效果
 *
 *  模块分工（考核讲解顺序）：
 *    LED_GpioInit()     普通推挽输出      → 只用"亮/灭"  （流水灯）
 *    LED_PwmInit()      复用输出 + TIM2   → 用"亮度"    （呼吸灯、波浪灯）
 *    Effect_xxx_Init()  进入效果时配置一次（含本效果的状态复位）
 *    Effect_xxx_Step()  只走一小步就返回（不阻塞！主循环才能一直扫按键）
 *
 *  ⚠️ 换效果必须重新配置引脚，别偷懒：
 *      PWM 模式下引脚是复用功能 → 写 ODR（GPIO_WriteBit）一点效果都没有；
 *      GPIO 模式下引脚是普通输出 → 改 CCR 看得到的是 0，不是亮度。
 ******************************************************************************
 */
#ifndef __LED_H
#define __LED_H

#include "stm32f10x.h"

/* ===================== 参数 ===================== */
#define LED_COUNT        3u      /* 板上 LED 数量（考核要求至少 3 个） */
#define EFFECT_STEP_MS   20u     /* 效果"走一小步"的节奏(ms)：想整体调快/调慢只改这一个数 */

/* ===================== 底层：两种驱动方式 ===================== */
void LED_GpioInit(void);    /* 引脚配成普通推挽输出 + 全灭（给"亮/灭"类效果用） */
void LED_PwmInit(void);     /* 引脚配成复用 + 启动 TIM2 PWM（给"亮度"类效果用） */
void LED_On(uint8_t idx);   /* idx: 0~2  只在 GPIO 模式下有效 */
void LED_Off(uint8_t idx);
void LED_AllOff(void);
void LED_SetBrightness(uint8_t idx, uint16_t duty);   /* duty: 0~1000  只在 PWM 模式下有效 */
uint16_t LED_GetBrightness(uint8_t idx);              /* 读回当前亮度（=CCR），Watch 观察用 */

/* ===================== 三种效果：Init + Step 成对出现 ===================== */
void Effect_Flow_Init(void);      void Effect_Flow_Step(void);       /* ① 传统流水灯 */
void Effect_Breathe_Init(void);   void Effect_Breathe_Step(void);    /* ② 呼吸灯（渐亮渐灭） */
void Effect_Wave_Init(void);      void Effect_Wave_Step(void);       /* ③ 亮度波浪（正弦，左→右） */

#endif /* __LED_H */
