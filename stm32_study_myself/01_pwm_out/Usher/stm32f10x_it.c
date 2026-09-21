/**
 ******************************************************************************
 * @file    stm32f10x_it.c
 * @brief   中断服务程序 (本项目 PWM 实验用不到任何中断, 只保留必要的异常处理)
 *
 * @note    PWM 完全由定时器硬件产生, CPU 不参与, 所以这里没有定时器中断。
 *          SysTick 也没有用中断(延时是轮询 COUNTFLAG 实现的)。
 *
 *          Cortex-M3 的异常向量必须存在, 否则启动文件里对应的弱定义被用到,
 *          一旦发生硬件错误会跑飞到 0xffffffff, 所以这里保留空实现。
 ******************************************************************************
 */

#include "stm32f10x.h"

/* ============================ Cortex-M3 内核异常 ============================ */
void NMI_Handler(void)        { }      /* 不可屏蔽中断 */
void HardFault_Handler(void)  { while (1) { } }   /* 硬件错误: 死循环, 方便调试时定位 */
void MemManage_Handler(void)  { while (1) { } }   /* 存储器管理错误 */
void BusFault_Handler(void)   { while (1) { } }   /* 总线错误 */
void UsageFault_Handler(void) { while (1) { } }   /* 用法错误 */
void SVC_Handler(void)        { }      /* 系统服务调用 */
void DebugMon_Handler(void)   { }      /* 调试监控 */
void PendSV_Handler(void)     { }      /* 可挂起系统服务 */
void SysTick_Handler(void)    { }      /* 本工程不使用 SysTick 中断 */

/* ============================ 外设中断 ============================ */
/* PWM 实验无需任何外设中断; 若以后要用, 在这里添加即可, 例如:

   void TIM3_IRQHandler(void)
   {
       if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET)
       {
           TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
       }
   }
*/
