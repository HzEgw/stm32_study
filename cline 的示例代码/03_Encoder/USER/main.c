/**
 ******************************************************************************
 * @file    main.c
 * @brief   编码器接口实验 —— 硬件四倍频计数 + 定时采样算转速/位置
 *
 *  ★ 怎么验证(不需要 LED / 串口):
 *     1) 把编码器 A 相接 PA6, B 相接 PA7, 编码器 GND 与板子共地, 编码器电源按规格接;
 *     2) 下载运行, 进调试模式, 把这些变量拖进 Watch:
 *          g_position  累计位置(计数)   —— 正转增大, 反转减小
 *          g_delta     每 50ms 的增量   —— 转得越快数值越大
 *          g_rpm       转速(rpm)        —— 带符号
 *          g_dir       方向: 0 正转 / 1 反转
 *          g_pwm_duty  送给 PB6 的占空比(千分之一) —— 与转速成正比
 *          g_samples   采样次数(每 50ms +1)
 *     3) 万用表直流档量 PB6: 转得越快电压越高(0~3.3V), 停转后回 0V —— 一眼看出转速;
 *     4) 没有编码器也能试: 用杜邦线把 PA6 或 PA7 快速碰一下 GND/3.3V,
 *        g_position 会增减, g_delta 会出现非零值(相当于"手动造脉冲")。
 *
 *  ★ 位置为什么要用 int32_t?
 *      TIM3->CNT 只有 16 位(±32767 就会翻转), 用"有符号差值"把它累加成 32 位,
 *      这样子可以一直转下去都不会出错 —— 见 encoder.c 里的注释。
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "encoder.h"

/* ============ Keil Watch 观察点 ============ */
volatile int32_t  g_position = 0;    /* 累计位置 */
volatile int32_t  g_delta    = 0;    /* 本采样周期增量 */
volatile int32_t  g_rpm      = 0;    /* 转速 rpm */
volatile uint8_t  g_dir      = 0;    /* 0 正转 / 1 反转 */
volatile uint16_t g_pwm_duty = 0;    /* PB6 占空比(千分比) */
volatile uint32_t g_samples  = 0;    /* 采样次数 */
volatile uint32_t g_reg_cnt  = 0;    /* TIM3->CNT 硬件真值 */
volatile uint32_t g_reg_smcr = 0;    /* TIM3->SMCR 快照(SMS=011 编码器模式) */

int main(void)
{
    SystemInit();                 /* 72MHz */

    Encoder_SpeedOutputInit();    /* PB6(TIM4_CH1) 输出与转速成正比的 PWM, 便于万用表观察 */
    Encoder_Init();               /* TIM3 编码器接口 + SysTick 1ms 节拍 */

    g_reg_smcr = TIM3->SMCR;      /* 快照一次, 对照 README 里的位域表 */

    while (1)
    {
        Encoder_Update();         /* 每 ENC_SAMPLE_MS(50ms) 才算一次, 其他时间立即返回 */

        g_position = Encoder_GetPosition();
        g_delta    = Encoder_GetDelta();
        g_rpm      = Encoder_GetRpm();
        g_dir      = Encoder_GetDirection();
        g_pwm_duty = Encoder_GetPwmDuty();
        g_samples  = Encoder_GetSamples();
        g_reg_cnt  = TIM3->CNT;   /* 直接看硬件计数, 会随你转动连续变化 */
    }
}
