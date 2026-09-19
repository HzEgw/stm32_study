/**
 ******************************************************************************
 * @file    main.c
 * @brief   串口通信实验 —— 中断接收 + 环形缓冲 + 帧协议（W2 的基础工程）
 *
 *  ★ 怎么验证（三种方式，任选其一）：
 *
 *  ① 有 USB-TTL 模块（最推荐）
 *       USB-TTL TX  →  PA10(RX)
 *       USB-TTL RX  →  PA9 (TX)
 *       GND         →  GND   （必须共地！）
 *     电脑打开串口助手：115200 / 8 / N / 1，用 HEX 显示
 *     应每 100ms 收到一帧：AA 55 04 xx xx 03 ss（xx 是计数器, 会增长）
 *     往串口发一帧同样格式的数据，会原样回显
 *
 *  ② 没有 USB-TTL —— 一根杜邦线自测（回环）
 *     短接 PA9 和 PA10，则自己发的帧会被自己收到 → g_rx_frames 会持续增长
 *
 *  ③ 用鱼香小车的蓝牙/ESP32 模块接 PA9/PA10（以后接 ROS2 就靠它）
 *
 *  ★ Keil Watch 观察点：g_tx_frames / g_rx_frames / g_last_len / g_last_p0 / g_rx_ovf
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "uart.h"

/* ============ 给 Keil Watch 窗口看的变量 ============ */
volatile uint32_t g_tick      = 0;    /* SysTick 1ms 计数 */
volatile uint32_t g_tx_frames = 0;    /* 已发送帧数 */
volatile uint32_t g_rx_frames = 0;    /* 已解析成功帧数 */
volatile uint16_t g_rx_ovf    = 0;    /* 接收缓冲溢出次数(应为 0) */
volatile uint8_t  g_last_len  = 0;    /* 最近一帧的长度 */
volatile uint8_t  g_last_p0   = 0;    /* 最近一帧的第 1 个数据字节 */
volatile uint8_t  g_last_p1   = 0;    /* 最近一帧的第 2 个数据字节 */

int main(void)
{
    uint8_t  payload[UART_FRAME_MAX_LEN];
    uint8_t  len     = 0u;
    uint32_t last_tx = 0u;
    uint16_t counter = 0u;

    SystemInit();                              /* 72MHz */
    UART_Init(UART_BAUD_DEFAULT);              /* USART1: PA9/PA10, 115200 */
    SysTick_Config(SystemCoreClock / 1000u);   /* 1ms 中断: 只做 g_tick++ */

    UART_SendString("USART1 ready @115200 8N1\r\n");

    while (1)
    {
        /* ---------- ① 接收: 解析出一帧就回显并记录 ---------- */
        if (UART_PollFrame(payload, &len) != 0u)
        {
            g_rx_frames++;
            g_last_len = len;
            g_last_p0  = payload[0];
            if (len > 1u)
            {
                g_last_p1 = payload[1];
            }

            UART_SendFrame(payload, len);      /* 原样回显, 方便上位机确认链路 */
            g_tx_frames++;
        }

        /* ---------- ② 发送: 每 100ms 上报一帧"遥测" ---------- */
        if ((g_tick - last_tx) >= 100u)
        {
            last_tx = g_tick;

            payload[0] = (uint8_t)(counter >> 8);            /* 计数器高字节 */
            payload[1] = (uint8_t)(counter & 0xFFu);         /* 计数器低字节 */
            payload[2] = (uint8_t)(g_rx_frames & 0xFFu);     /* 回显计数 */
            payload[3] = 0x03u;                              /* 固定标识 */

            UART_SendFrame(payload, 4u);                     /* 帧头+长度+数据+校验和 */
            counter++;
            g_tx_frames++;
        }

        g_rx_ovf = UART_GetRxOverflow();
        /* 主循环保持"轻": 真正耗时的解析/计算都在这里做, 中断只搬数据 */
    }
}
