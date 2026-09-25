/**
 ******************************************************************************
 * @file    stm32f10x_it.c
 * @brief   中断服务程序 —— 寄存器版编码器工程
 *
 *   SysTick_Handler : 1ms 节拍 -> 每 50ms 置一次采样标志
 *
 *  ★ 编码器接口模式本身不需要中断: A/B 相计数与方向判定全在定时器硬件里完成,
 *    这也是为什么本文件里没有 TIM3_IRQHandler(不是漏写)。
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "encoder_reg.h"

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
    Encoder_SysTickHandler();
}
