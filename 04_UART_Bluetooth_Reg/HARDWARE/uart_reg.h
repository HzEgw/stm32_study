/**
 ******************************************************************************
 * @file    uart_reg.h
 * @brief   串口驱动 —— 纯寄存器版 (USART1@115200, 中断接收 + 环形缓冲 + 帧协议)
 *
 *  功能与 04_UART_Bluetooth(标准库版) 完全一致, 只是初始化全部直接写寄存器。
 *  帧格式: 0xAA 0x55 | LEN(1B) | PAYLOAD[LEN] | SUM(1B)   SUM = (LEN + ΣPAYLOAD) & 0xFF
 *
 * ============================ 寄存器速查表 ============================
 *  寄存器          作用                 本文件用到的位
 *  --------------  -------------------  ---------------------------------
 *  RCC->APB2ENR    时钟使能              bit0 AFIOEN, bit2 IOPAEN, bit14 USART1EN
 *  RCC->CFGR       时钟配置              bit13:11 PPRE2[2:0]  (算 PCLK2)
 *  GPIOA->CRH      引脚 8~15             PA9(bit7:4)=0xB 复用推挽, PA10(bit11:8)=0x4 浮空输入
 *  USART1->BRR     波特率                BRR = round(PCLK2 / 波特率)  (含 1/16 小数)
 *  USART1->CR1     控制 1                bit13 UE 使能, bit3 TE 发送, bit2 RE 接收,
 *                                        bit5 RXNEIE 接收中断, bit12 M 字长, bit10 PCE 校验
 *  USART1->CR2     控制 2                bit13:12 STOP 停止位
 *  USART1->CR3     控制 3                硬件流控/DMA(本工程全 0)
 *  USART1->SR      状态                  bit5 RXNE 收到数据, bit7 TXE 发送寄存器空
 *  USART1->DR      数据                  读=收到的字节, 写=要发的字节
 *  NVIC->ISER/IPR  中断                 CMSIS 的 NVIC_EnableIRQ / NVIC_SetPriority
 * =====================================================================
 ******************************************************************************
 */
#ifndef __UART_REG_H
#define __UART_REG_H

#include "stm32f10x.h"

/* ===================== 参数配置 ===================== */
#define UART_BAUD_DEFAULT   115200u
#define UART_RX_BUF_SIZE    128u          /* 必须是 2 的幂(为了 & 取模) */
#define UART_FRAME_HEAD0    0xAAu
#define UART_FRAME_HEAD1    0x55u
#define UART_FRAME_MAX_LEN  32u

/* ===================== 对外接口 ===================== */
void     UART_Init(uint32_t baudrate);

void     UART_SendByte(uint8_t b);
void     UART_SendBuf(const uint8_t *buf, uint16_t len);
void     UART_SendString(const char *s);

uint16_t UART_RxAvailable(void);
int16_t  UART_ReadByte(void);
uint16_t UART_GetRxOverflow(void);

void     UART_SendFrame(const uint8_t *payload, uint8_t len);
uint8_t  UART_PollFrame(uint8_t *payload, uint8_t *len);

void     UART_IRQHandler(void);

#endif /* __UART_REG_H */
