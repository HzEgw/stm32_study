/**
 ******************************************************************************
 * @file    stm32f10x_it.c
 * @brief   中断服务程序 —— 本工程只用 1 个中断（与标准库版相同）
 *
 *   SysTick_Handler : 1ms 节拍(主循环用它做 5ms 采样)
 *
 *  ★ 为什么没有 I2C1_ER_IRQHandler / I2C1_EV_IRQHandler:
 *    本工程 I2C 走"查询 + 超时"(见 mpu6050_reg.c), **不产生中断**;
 *    MPU6050 的 INT 引脚也不用(轮询 200Hz 毫无压力)。
 *    中断方式等你 W8 之后做 micro-ROS 时再上(那时才需要真正的并发)。
 ******************************************************************************
 */

#include "stm32f10x.h"

extern volatile uint32_t g_tick;

/* ============================ Cortex-M3 内核异常 ============================ */
void NMI_Handler(void)        { }
void HardFault_Handler(void)  { while (1) { } }
void MemManage_Handler(void)  { while (1) { } }
void BusFault_Handler(void)   { while (1) { } }
void UsageFault_Handler(void) { while (1) { } }
void SVC_Handler(void)        { }
void DebugMon_Handler(void)   { }
void PendSV_Handler(void)     { }

/* ============================ 用到的中断 ============================ */
void SysTick_Handler(void)
{
    g_tick++;
}

/* 本工程没有 TIM3 / I2C1 中断 —— 不是漏写: I2C 用轮询+超时, 定时器只用来输出 PWM */