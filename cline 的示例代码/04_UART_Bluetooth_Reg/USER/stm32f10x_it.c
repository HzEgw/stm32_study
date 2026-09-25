/**
 ******************************************************************************
 * @file    stm32f10x_it.c
 * @brief   中断服务程序 —— 寄存器版串口工程
 *
 *   USART1_IRQHandler : 收到字节 -> uart_reg.c 搬进环形缓冲
 *   SysTick_Handler   : 1ms 时间基准
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "uart_reg.h"

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

void USART1_IRQHandler(void)
{
    UART_IRQHandler();
}
