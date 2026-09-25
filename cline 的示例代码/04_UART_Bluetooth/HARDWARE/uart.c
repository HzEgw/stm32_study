/**
 ******************************************************************************
 * @file    uart.c
 * @brief   串口驱动实现（标准库版）—— 中断接收 + 环形缓冲 + 帧协议状态机
 *
 *  每条库函数后面都注明实际操作的寄存器, 便于对照寄存器版(04_UART_Bluetooth_Reg)。
 ******************************************************************************
 */

#include "uart.h"

/* ============================ 私有变量 ============================ */
static volatile uint8_t  s_rx_buf[UART_RX_BUF_SIZE];
static volatile uint16_t s_rx_head = 0u;      /* 中断里写 */
static volatile uint16_t s_rx_tail = 0u;      /* 主循环读 */
static volatile uint16_t s_rx_ovf  = 0u;      /* 环形缓冲写满的次数 */

/* ============================ 初始化 ============================ */
/**
 * @brief  初始化 USART1 (PA9 = TX, PA10 = RX), 开启接收中断
 */
void UART_Init(uint32_t baudrate)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1 | RCC_APB2Periph_AFIO, ENABLE);
    /* ↑ 实际寄存器操作: RCC->APB2ENR |= (1<<2)|(1<<14)|(1<<0);  (IOPAEN / USART1EN / AFIOEN) */

    /* PA9 = TX: 复用推挽输出 50MHz -> GPIOA->CRH 的 bit7:4 = 0xB */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* PA10 = RX: 浮空输入 -> GPIOA->CRH 的 bit11:8 = 0x4 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate            = baudrate;                 /* -> USART1->BRR */
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;      /* -> CR1 的 M(bit12)=0 */
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;         /* -> CR2 的 STOP[13:12]=00 */
    USART_InitStructure.USART_Parity              = USART_Parity_No;          /* -> CR1 的 PCE(bit10)=0 */
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    /* ↑ 模式位 -> USART1->CR1 的 RE(bit2) / TE(bit3) */
    USART_Init(USART1, &USART_InitStructure);

    /* 打开"收到一个字节就中断": USART1->CR1 |= (1<<5)  RXNEIE */
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel                   = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_Cmd(USART1, ENABLE);       /* -> USART1->CR1 |= (1<<13)  UE 串口使能 */
}

/* ============================ 中断处理 ============================ */
/**
 * @brief  USART1 中断: 只把数据搬进环形缓冲, 不做解析(解析放主循环)
 * @note   读 DR 会自动清 RXNE 标志
 */
void UART_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)   /* 读 USART1->SR 的 RXNE(bit5) */
    {
        uint8_t  data = (uint8_t)USART_ReceiveData(USART1);  /* 读 USART1->DR, 同时清标志 */
        uint16_t next = (uint16_t)((s_rx_head + 1u) & (UART_RX_BUF_SIZE - 1u));

        if (next == s_rx_tail)
        {
            s_rx_ovf++;          /* 缓冲满了: 丢弃并计数(中断里绝不死等!) */
        }
        else
        {
            s_rx_buf[s_rx_head] = data;
            s_rx_head = next;
        }
    }
}

/* ============================ 发送(阻塞) ============================ */
void UART_SendByte(uint8_t b)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET)   /* 等 USART1->SR 的 TXE(bit7) */
    {
    }
    USART_SendData(USART1, b);                                     /* 写 USART1->DR */
}

void UART_SendBuf(const uint8_t *buf, uint16_t len)
{
    uint16_t i;
    for (i = 0u; i < len; i++)
    {
        UART_SendByte(buf[i]);
    }
}

void UART_SendString(const char *s)
{
    while (*s != '\0')
    {
        UART_SendByte((uint8_t)(*s));
        s++;
    }
}

/* ============================ 接收(环形缓冲) ============================ */
/**
 * @brief  缓冲区里还有多少字节可读
 * @note   head/tail 都是 16 位且缓冲区大小是 2 的幂, 用 & 取模比 % 快
 */
uint16_t UART_RxAvailable(void)
{
    return (uint16_t)((s_rx_head - s_rx_tail) & (UART_RX_BUF_SIZE - 1u));
}

/**
 * @brief  非阻塞取 1 字节
 * @retval >=0 取到的字节; -1 表示当前没有数据
 */
int16_t UART_ReadByte(void)
{
    int16_t b = -1;

    if (s_rx_head != s_rx_tail)
    {
        b = (int16_t)s_rx_buf[s_rx_tail];
        s_rx_tail = (uint16_t)((s_rx_tail + 1u) & (UART_RX_BUF_SIZE - 1u));
    }
    return b;
}

uint16_t UART_GetRxOverflow(void)
{
    return s_rx_ovf;
}

/* ============================ 帧协议 ============================ */
/**
 * @brief  打包发送一帧: 0xAA 0x55 | LEN | PAYLOAD[LEN] | SUM
 *         SUM = (LEN + ΣPAYLOAD) & 0xFF
 */
void UART_SendFrame(const uint8_t *payload, uint8_t len)
{
    uint8_t sum;
    uint8_t i;

    if ((payload == 0) || (len == 0u) || (len > UART_FRAME_MAX_LEN))
    {
        return;
    }

    sum = len;
    for (i = 0u; i < len; i++)
    {
        sum = (uint8_t)(sum + payload[i]);
    }

    UART_SendByte(UART_FRAME_HEAD0);
    UART_SendByte(UART_FRAME_HEAD1);
    UART_SendByte(len);
    UART_SendBuf(payload, len);
    UART_SendByte(sum);
}

/**
 * @brief  把环形缓冲里的字节喂给状态机, 解析出完整帧就返回 1
 * @param  payload 输出缓冲(至少 UART_FRAME_MAX_LEN 字节); len 输出实际长度
 * @note   非阻塞, 适合在主循环里反复调用
 *         状态: 0 等 0xAA -> 1 等 0x55 -> 2 取 LEN -> 3 收 PAYLOAD -> 4 校验 SUM
 */
uint8_t UART_PollFrame(uint8_t *payload, uint8_t *len)
{
    static uint8_t state = 0u;
    static uint8_t idx   = 0u;
    static uint8_t need  = 0u;
    static uint8_t sum   = 0u;
    int16_t        b;

    while ((b = UART_ReadByte()) >= 0)
    {
        uint8_t data = (uint8_t)b;

        switch (state)
        {
            case 0u:                                          /* 找帧头第一字节 */
                if (data == UART_FRAME_HEAD0)
                {
                    state = 1u;
                }
                break;

            case 1u:                                          /* 找帧头第二字节 */
                if (data == UART_FRAME_HEAD1)      { state = 2u; }
                else if (data == UART_FRAME_HEAD0) { state = 1u; }   /* 0xAA 0xAA 也能接上 */
                else                               { state = 0u; }
                break;

            case 2u:                                          /* 取长度 */
                if ((data == 0u) || (data > UART_FRAME_MAX_LEN))
                {
                    state = 0u;                               /* 长度非法: 丢帧重来 */
                }
                else
                {
                    need  = data;
                    idx   = 0u;
                    sum   = data;                             /* 校验和从 LEN 开始累加 */
                    state = 3u;
                }
                break;

            case 3u:                                          /* 收数据 */
                payload[idx] = data;
                idx++;
                sum = (uint8_t)(sum + data);
                if (idx >= need)
                {
                    state = 4u;
                }
                break;

            case 4u:                                          /* 校验 */
                state = 0u;                                   /* 无论对错都回到找帧头 */
                if (data == sum)
                {
                    *len = need;
                    return 1u;                                /* ✅ 完整且校验通过 */
                }
                break;

            default:
                state = 0u;
                break;
        }
    }
    return 0u;
}
