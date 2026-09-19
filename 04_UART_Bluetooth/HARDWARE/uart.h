/**
 ******************************************************************************
 * @file    uart.h
 * @brief   串口驱动（标准库版）—— USART1@115200, 接收用"中断 + 环形缓冲"
 *
 *  为什么要做这个工程(W2 的核心):
 *    - 串口是 MCU 与上位机(ROS2)通信的地基: 后面 micro-ROS、蓝牙调参、上位机画曲线都靠它;
 *    - 中断 + 环形缓冲, 是"中断里只搬数据、主循环慢慢处理"的标准做法;
 *    - 顺手定义一套**帧协议**, 主循环里用状态机解析 -> 这就是"协议桥"的最小原型。
 *
 *  引脚: PA9 = USART1_TX, PA10 = USART1_RX (不占用 SWD 的 PA13/PA14)
 *
 *  帧格式(小端无关, 单字节字段):
 *      0xAA 0x55 | LEN(1B) | PAYLOAD[LEN] | SUM(1B)
 *      SUM = (LEN + 所有 PAYLOAD 字节) & 0xFF
 *      LEN 范围 1~32
 *
 *  接收: USART1 中断 -> 环形缓冲(RX)
 *  发送: 轮询 TXE 阻塞发送(初学够用; 想进阶可改成 TX 中断/DMA — 见 README 练习)
 ******************************************************************************
 */
#ifndef __UART_H
#define __UART_H

#include "stm32f10x.h"

/* ===================== 参数配置 ===================== */
#define UART_BAUD_DEFAULT   115200u
#define UART_RX_BUF_SIZE    128u          /* 必须是 2 的幂(为了 & 取模) */
#define UART_FRAME_HEAD0    0xAAu
#define UART_FRAME_HEAD1    0x55u
#define UART_FRAME_MAX_LEN  32u

/* ===================== 对外接口 ===================== */
void     UART_Init(uint32_t baudrate);

/* --- 发送(阻塞, 内部等 TXE) --- */
void     UART_SendByte(uint8_t b);
void     UART_SendBuf(const uint8_t *buf, uint16_t len);
void     UART_SendString(const char *s);

/* --- 接收(非阻塞, 从环形缓冲取) --- */
uint16_t UART_RxAvailable(void);          /* 缓冲区里还有多少字节 */
int16_t  UART_ReadByte(void);             /* 取 1 字节; 无数据返回 -1 */
uint16_t UART_GetRxOverflow(void);        /* 溢出计数(环形缓冲被写满的次数) */

/* --- 帧协议 --- */
void     UART_SendFrame(const uint8_t *payload, uint8_t len);
uint8_t  UART_PollFrame(uint8_t *payload, uint8_t *len);   /* 解析出完整一帧返回 1 */

/* --- 中断入口: 由 stm32f10x_it.c 的 USART1_IRQHandler 调用 --- */
void     UART_IRQHandler(void);

#endif /* __UART_H */
