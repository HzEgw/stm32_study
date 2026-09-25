/**
 ******************************************************************************
 * @file    stm32f10x_it.c
 * @brief   中断服务程序 —— 本工程只用 1 个中断
 *
 *   SysTick_Handler : 1ms 节拍(g_kit_tick)
 *     用途: ① Kit_DelayMs 的非阻塞延时
 *           ② 主循环的 10ms 编码器采样节拍 / 500ms 打印节拍
 *
 *  ★ 为什么中断里只 +1 就够:
 *     本工程是"验收工具", 所有实时性要求都在毫秒级; 真正的控制(以后做 PID)
 *     也是 10ms 级的循环 —— 所以中断里只做"置节拍"这一件事, 处理全放主循环,
 *     这不只是习惯, 更是避免"中断里做事太多"的工程纪律。
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "kit_check.h"

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
    g_kit_tick++;
}

/* 本工程没有 TIM2/TIM3/TIM4/ADC1/USART1/I2C2 中断 —— 全部走查询, 不是漏写 */