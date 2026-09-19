/**
 ******************************************************************************
 * @file    main.c
 * @brief   套件到货验收 (06_Kit_Check) —— 上电自动自检 + 常规观察模式
 *
 *  用法:
 *    ① 烧录后用 **USB-TTL(115200 8N1)** 接 PA9/PA10 看串口输出;
 *       没串口也行 —— 用 Keil Watch 看 `g_kit_pass[]` / `g_kit_pass_cnt`。
 *    ② 上电自动跑"一键自检", **需要你配合两件事**: 3 秒内按一下按键、3 秒内手转两个轮子;
 *    ③ 自检完成后进入常规模式: 每 500ms 打印两轮计数/转速/电池电压; 按按键切换电机1 开/停。
 *
 *  ⚠️ 接线前必做(安全):
 *    · 电机驱动板的**电源地**与 STM32 的 **GND 必须共地**;
 *    · 12V 只能进驱动板的 VM, **绝不能进 STM32 板**(见 kit_config.h 注释);
 *    · 第一次上电先用万用表量驱动板逻辑供电是 3.3V 还是 5V, 再决定信号电平。
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "kit_check.h"
#include "kit_config.h"

/* ============ Watch 观察点 ============ */
volatile uint32_t g_loop_cnt = 0;      /* 主循环轮数(证明程序没卡死) */
volatile uint32_t g_kit_key_cnt = 0;   /* 按键按下次数 */

int main(void)
{
    uint32_t last_sample = 0u;
    uint32_t last_print  = 0u;
    uint8_t  motor_on    = 0u;
    uint8_t  key_pre     = 0u;

    SystemInit();                               /* 72MHz (APB1 = 36MHz) */
    SysTick_Config(SystemCoreClock / 1000u);    /* 1ms 节拍: 自检延时/采样节拍都用它 */

    Kit_Init();                                 /* 全部外设初始化(见 kit_check.c) */

    Kit_Print("\r\n=================================================\r\n");
    Kit_Print(" 06_Kit_Check  套件到货验收工具\r\n");
    Kit_Print(" 引脚映射: HARDWARE\\kit_config.h (到货后按丝印核对!)\r\n");
    Kit_Print("=================================================\r\n");

    Kit_SelfTest();                             /* 一键自检(需人配合 2 项) */

    Kit_Print("\r\n-- 进入常规模式: 每 500ms 打印轮速; 按键切换 电机1 正转/停 --\r\n");

    while (1)
    {
        g_loop_cnt++;

        /* ① 10ms 采样节拍: 读编码器(累加计数 + 算转速) */
        if ((uint32_t)(g_kit_tick - last_sample) >= KIT_ENC_SAMPLE_MS)
        {
            last_sample = g_kit_tick;
            Kit_EncoderUpdate();
        }

        /* ② 500ms 打印一次状态 */
        if ((uint32_t)(g_kit_tick - last_print) >= 500u)
        {
            last_print = g_kit_tick;
            Kit_Print("enc1=");  Kit_PrintI32(g_kit_enc_total[0]);
            Kit_Print(" rpm1x100="); Kit_PrintI32(g_kit_rpm_x100[0]);
            Kit_Print(" | enc2=");   Kit_PrintI32(g_kit_enc_total[1]);
            Kit_Print(" rpm2x100="); Kit_PrintI32(g_kit_rpm_x100[1]);
            Kit_Print(" | bat=");    Kit_PrintU32(g_kit_bat_mv);
            Kit_Print("mV\r\n");
        }

        /* ③ 按键: 上升沿切换电机1(带 20ms 简单消抖) */
        if ((Kit_KeyPressed() != 0u) && (key_pre == 0u))
        {
            Kit_DelayMs(20u);
            if (Kit_KeyPressed() != 0u)
            {
                motor_on = (motor_on == 0u) ? 1u : 0u;
                Kit_MotorSet(0u, (motor_on != 0u) ? (int16_t)300 : (int16_t)0);
                g_kit_key_cnt++;
                Kit_Print((motor_on != 0u) ? "电机1 正转(duty=300)\r\n" : "电机1 停\r\n");
            }
        }
        key_pre = Kit_KeyPressed();
    }
}