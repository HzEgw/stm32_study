/**
 ******************************************************************************
 * @file    main.c
 * @brief   串口通信实验 —— 寄存器版 (功能与 04_UART_Bluetooth 完全相同)
 *
 *  验证方式:
 *    ① USB-TTL: TX→PA10, RX→PA9, GND→GND; 串口助手 115200/8/N/1, HEX 显示
 *       每 100ms 收到: AA 55 04 xx yy 03 ss   (ss = (0x04+xx+yy+0x03) & 0xFF)
 *    ② 没有 USB-TTL: 一根杜邦线短接 PA9–PA10 做回环自测 → g_rx_frames 会增长
 *
 *  Watch 变量: g_tx_frames / g_rx_frames / g_last_len / g_last_p0 / g_tx_brr / g_tx_cr1
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "uart_reg.h"

/* ============ Watch 观察点 ============ */
volatile uint32_t g_tick      = 0;
volatile uint32_t g_tx_frames = 0;
volatile uint32_t g_rx_frames = 0;
volatile uint16_t g_rx_ovf    = 0;
volatile uint8_t  g_last_len  = 0;
volatile uint8_t  g_last_p0   = 0;
volatile uint8_t  g_last_p1   = 0;

/* 直接看寄存器真值, 便于核对初始化是否正确 */
volatile uint32_t g_reg_brr = 0;    /* 应为 625 (0x271) @72MHz/115200 */
volatile uint32_t g_reg_cr1 = 0;    /* UE|RXNEIE|TE|RE = 0x2020 | 0xC = 0x202C */
volatile uint32_t g_reg_crh = 0;    /* PA9=0xB(bit7:4), PA10=0x4(bit11:8) -> 0x4B0 */

int main(void)
{
    uint8_t  payload[UART_FRAME_MAX_LEN];
    uint8_t  len     = 0u;
    uint32_t last_tx = 0u;
    uint16_t counter = 0u;

    SystemInit();                              /* 72MHz: FLASH->ACR / RCC->CFGR(PLL ×9) / RCC->CR */
    UART_Init(UART_BAUD_DEFAULT);              /* 全部寄存器操作, 见 uart_reg.c */

    g_reg_brr = USART1->BRR;
    g_reg_cr1 = USART1->CR1;
    g_reg_crh = GPIOA->CRH;

    SysTick_Config(SystemCoreClock / 1000u);   /* CMSIS 函数: 设好 LOAD/VAL/CTRL */
    UART_SendString("USART1(reg version) ready @115200 8N1\r\n");

    while (1)
    {
        /* ① 收: 解析出一帧就回显 */
        if (UART_PollFrame(payload, &len) != 0u)
        {
            g_rx_frames++;
            g_last_len = len;
            g_last_p0  = payload[0];
            if (len > 1u) { g_last_p1 = payload[1]; }
            UART_SendFrame(payload, len);
            g_tx_frames++;
        }

        /* ② 发: 每 100ms 上报一帧 */
        if ((g_tick - last_tx) >= 100u)
        {
            last_tx = g_tick;
            payload[0] = (uint8_t)(counter >> 8);
            payload[1] = (uint8_t)(counter & 0xFFu);
            payload[2] = (uint8_t)(g_rx_frames & 0xFFu);
            payload[3] = 0x03u;
            UART_SendFrame(payload, 4u);
            counter++;
            g_tx_frames++;
        }

        g_rx_ovf = UART_GetRxOverflow();
    }
}
