/**
 ******************************************************************************
 * @file    stm32f10x_it.c
 * @brief   中断服务程序 —— 编码器工程只用到 1 个中断
 *
 *   SysTick_Handler : 1ms 节拍, 每 50ms 置一次"该采样了"的标志
 *
 *  ★ 注意: 编码器接口模式**不需要任何定时器中断**!
 *    A/B 相的计数和方向判定全部由定时器硬件完成, 这是它比"外部中断计数法"
 *    优秀的地方 —— CPU 占用为 0, 而且不会丢脉冲。
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "encoder.h"

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
 * @brief  SysTick 1ms: 打节拍(中断里只做 ++ 和置标志, 不做除法)
 */
void SysTick_Handler(void)
{
    Encoder_SysTickHandler();
}

/* TIM3 编码器模式不产生中断, 所以这里没有 TIM3_IRQHandler —— 不是漏写 */
