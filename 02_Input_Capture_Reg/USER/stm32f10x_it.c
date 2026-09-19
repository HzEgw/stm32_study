/**
 ******************************************************************************
 * @file    stm32f10x_it.c
 * @brief   中断服务程序 —— 寄存器版
 *
 *   TIM3_IRQHandler : 输入捕获(上升沿/下降沿) -> 读 TIM3->CCR1 / CCR2
 *   SysTick_Handler : 1ms 计时 -> "无信号超时"判断
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "ic_reg.h"

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
 * @brief  SysTick 1ms: 只做超时计数
 */
void SysTick_Handler(void)
{
    IC_SysTickHandler();
}

/**
 * @brief  TIM3 捕获中断: 读 CCR1(周期) / CCR2(高电平)
 * @note   函数体内的判断与清标志都在 ic_reg.c 里, 这里只转发
 */
void TIM3_IRQHandler(void)
{
    IC_TIM3_IRQHandler();
}
