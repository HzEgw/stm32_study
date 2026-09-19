/**
 ******************************************************************************
 * @file    stm32f10x_it.c
 * @brief   中断服务程序 —— 本工程用到 2 个中断
 *
 *   USART1_IRQHandler : 收到一个字节 -> 交给 uart.c 搬进环形缓冲
 *   SysTick_Handler   : 1ms 时间基准(只做 ++, 主循环用它做 100ms 定时)
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "uart.h"

/* main.c 里定义的 1ms 计数(跨文件使用, 所以用 extern 声明) */
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
/**
 * @brief  SysTick 1ms: 只累加时间基准
 */
void SysTick_Handler(void)
{
    g_tick++;
}

/**
 * @brief  USART1 接收中断: 转发给驱动
 */
void USART1_IRQHandler(void)
{
    UART_IRQHandler();
}
