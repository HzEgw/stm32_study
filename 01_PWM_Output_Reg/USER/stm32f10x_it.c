/**
 ******************************************************************************
 * @file    stm32f10x_it.c
 * @brief   中断服务程序 —— 寄存器版工程同样不用任何中断
 *
 * @note    PWM 全部由定时器硬件产生, CPU 不参与; SysTick 用轮询也不用中断。
 *          Cortex-M3 的异常向量必须存在(启动文件里是弱定义, 这里给出实体),
 *          否则一旦发生硬件错误会跑到无效地址。
 ******************************************************************************
 */

#include "stm32f10x.h"

/* ============================ Cortex-M3 内核异常 ============================ */
void NMI_Handler(void)        { }
void HardFault_Handler(void)  { while (1) { } }
void MemManage_Handler(void)  { while (1) { } }
void BusFault_Handler(void)   { while (1) { } }
void UsageFault_Handler(void) { while (1) { } }
void SVC_Handler(void)        { }
void DebugMon_Handler(void)   { }
void PendSV_Handler(void)     { }
void SysTick_Handler(void)    { }

/* ============================ 外设中断 ============================ */
/* 本工程不需要, 留空即可。将来要用时在这里补, 例如:

   void TIM3_IRQHandler(void)
   {
       if ((TIM3->SR & (1u << 0)) != 0u)     // bit0 UIF 更新中断标志
       {
           TIM3->SR = ~(1u << 0);            // 写 0 清标志(注意是"写0清")
       }
   }
*/
