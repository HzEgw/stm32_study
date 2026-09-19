/**
 ******************************************************************************
 * @file    stm32f10x_it.c
 * @brief   中断服务程序 —— 本工程用到 2 个中断
 *
 *   TIM3_IRQHandler   : 输入捕获中断(上升沿/下降沿)
 *   SysTick_Handler   : 1ms 计时, 用于"无信号超时"判断
 *
 *  注意: 中断函数里只做"取数据 + 置标志", 具体处理留给主循环 —— 好习惯。
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "ic.h"

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
/**
 * @brief  SysTick 1ms 中断: 交给驱动做超时计时
 */
void SysTick_Handler(void)
{
    IC_SysTickHandler();
}

/**
 * @brief  TIM3 捕获中断: 读取 CCR1(周期) / CCR2(高电平时间)
 * @note   具体计算在 ic.c 里, 这里只做转发, 便于集中管理
 */
void TIM3_IRQHandler(void)
{
    IC_TIM3_IRQHandler();
}
