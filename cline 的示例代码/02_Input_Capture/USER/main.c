/**
 ******************************************************************************
 * @file    main.c
 * @brief   输入捕获实验 —— PWM 输入模式, 同时测出"频率"和"占空比"
 *
 *  ★ 一块板就能自测(不需要信号发生器, 也不需要 LED/串口):
 *     1) 用一根杜邦线把 PB6 接到 PA6
 *        (PB6 是本工程自己产生的 1kHz / 30% 方波, PA6 是捕获输入)
 *     2) 下载运行, 进调试模式, 把这些变量拖进 Keil 的 Watch 窗口:
 *          g_freq_hz  = 1000     ← 1kHz
 *          g_period   = 1000     ← 周期 1000us
 *          g_high     = 300      ← 高电平 300us
 *          g_duty     = 300      ← 占空比 30.0%
 *          g_captures 不停增大    ← 捕获在持续发生
 *     3) 把跳线拔掉 → 100ms 后 g_lost 变 1、g_freq_hz 变 0 (超时检测生效)
 *
 *  ★ 想测别的信号: 信号源的地要和本板共地, 信号接 PA6(或 PA7 也能测到周期)
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "ic.h"

/* ============ Keil Watch 观察点 ============ */
volatile uint32_t g_freq_hz  = 0;
volatile uint16_t g_period   = 0;
volatile uint16_t g_high     = 0;
volatile uint16_t g_duty     = 0;
volatile uint32_t g_captures = 0;
volatile uint8_t  g_lost     = 0;
volatile uint32_t g_ccr1_reg = 0;      /* 直接读 TIM3->CCR1, 看硬件真实值 */

int main(void)
{
    SystemInit();            /* 72MHz; 内部就是 FLASH->ACR / RCC->CFGR / RCC->CR 那几步 */

    IC_TestSignalInit();     /* PB6 输出 1kHz/30% 方波 (不接外部信号源时用) */
    IC_Init();               /* PA6/PA7 输入捕获 + SysTick 1ms 超时 */

    while (1)
    {
        g_freq_hz  = IC_GetFreqHz();
        g_period   = IC_GetPeriodUs();
        g_high     = IC_GetHighUs();
        g_duty     = IC_GetDutyPermille();
        g_captures = IC_GetCaptureCount();
        g_lost     = IC_IsSignalLost();
        g_ccr1_reg = TIM3->CCR1;      /* 寄存器级观察: 一个周期的计数值(us) */
    }
}
