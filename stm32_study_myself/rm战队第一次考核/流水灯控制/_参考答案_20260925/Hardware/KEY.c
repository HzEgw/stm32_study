/**
 ******************************************************************************
 * @file    KEY.c
 * @brief   板载按键 PA0 的"非阻塞 + 消抖"扫描
 *
 *  ============ 为什么不用"Delay 消抖 + while 等松手"？ ============
 *    那种写法会把主循环卡住：等松手的那几百毫秒里，呼吸灯/波浪灯整个停住；
 *    而且松手瞬间容易连报多次（表现为"按一下跳两个效果"）。
 *
 *  ============ 这里的做法（状态消抖，教科书经典写法） ============
 *    每调用一次 = 采样一次；只有"连续 KEY_DEBOUNCE_CNT 次"都读到同一种电平，
 *    才承认电平真的变了。主循环 20ms 调一次 → 40ms 完成消抖。
 *    返回 1 = 检测到一次"可靠按下"（按下的那一刻报一次）；
 *    按住不放不会重复报，必须松开再按才算新的一次。
 *
 *  ⚠️ 如果你的板子按键是"按下为高电平"：把 KEY_PRESSED 改成 1、
 *     GPIO_Mode_IPU 改成 GPIO_Mode_IPD 即可（只需改这两行）。
 ******************************************************************************
 */

#include "KEY.h"

#define KEY_PORT          GPIOA
#define KEY_PIN           GPIO_Pin_0
#define KEY_PRESSED       0u     /* 按下时读到的电平：0 = 按下接 GND */
#define KEY_DEBOUNCE_CNT  2u     /* 连续几次采样一致才算稳定（2 × 20ms = 40ms） */

/**
 * @brief  初始化按键引脚：PA0 上拉输入
 * @note   上拉输入 = 内部把引脚"轻轻拉到 3.3V"
 *         → 不按 = 读 1（高），按下（另一端接 GND）= 读 0（低）
 *         -> 实际寄存器：GPIOA->CRL 的 bit3:0 = 0x8（CNF=10 上下拉输入, MODE=00 输入）
 *         -> 同时 GPIOA->ODR 的 bit0 = 1 才叫"上拉"（库函数 GPIO_Init 帮你写了）
 */
void KEY_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);   /* RCC->APB2ENR |= (1<<2) */

    GPIO_InitStructure.GPIO_Pin   = KEY_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPU;          /* 上拉输入 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;       /* 输入模式下该参数无实际作用 */
    GPIO_Init(KEY_PORT, &GPIO_InitStructure);
}

/**
 * @brief  扫描按键（非阻塞，每次调用立刻返回）
 * @retval 1 = 有一次新按下；0 = 无事件
 */
uint8_t KEY_Scan(void)
{
    static uint8_t s_stable = 1u;   /* 消抖后的"稳定电平"，上电时没按 = 1 */
    static uint8_t s_cnt    = 0u;   /* 连续读到"和稳定电平不一样"的次数 */
    uint8_t        raw;             /* 本次采样到的原始电平 */

    raw = (uint8_t)GPIO_ReadInputDataBit(KEY_PORT, KEY_PIN);
    /* ↑ 读的是 IDR 寄存器：GPIOA->IDR 的 bit0 */

    if (raw != s_stable)
    {
        s_cnt++;
        if (s_cnt >= KEY_DEBOUNCE_CNT)      /* 连续 N 次都一样 → 承认电平真的变了 */
        {
            s_stable = raw;
            s_cnt    = 0u;

            if (s_stable == KEY_PRESSED)
            {
                return 1u;                  /* 报一次"按下"事件 */
            }
        }
    }
    else
    {
        s_cnt = 0u;                         /* 中间有一次抖回去 → 重新数 */
    }

    return 0u;
}
