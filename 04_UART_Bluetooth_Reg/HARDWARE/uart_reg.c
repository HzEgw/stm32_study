/**
 ******************************************************************************
 * @file    uart_reg.c
 * @brief   串口驱动 —— 纯寄存器实现
 *
 *  配置顺序(与标准库版一一对应):
 *    ① 开时钟 ② 配 PA9/PA10 ③ 算 BRR 设置波特率 ④ CR1/CR2 配帧格式并开中断
 *    ⑤ NVIC 使能中断 ⑥ 环形缓冲 + 帧解析(纯软件, 与标准库版完全相同)
 ******************************************************************************
 */

#include "uart_reg.h"

/* ============================ 私有变量 ============================ */
static volatile uint8_t  s_rx_buf[UART_RX_BUF_SIZE];
static volatile uint16_t s_rx_head = 0u;      /* 中断里写 */
static volatile uint16_t s_rx_tail = 0u;      /* 主循环读 */
static volatile uint16_t s_rx_ovf  = 0u;

/* ============================ 私有函数 ============================ */
static uint32_t UART_GetPclk2(void);

/**
 * @brief  求 APB2 时钟 PCLK2 (USART1 挂 APB2)
 * @note   RCC->CFGR 的 bit13:11 = PPRE2[2:0]; 0xx 不分频, 100=/2, 101=/4, 110=/8, 111=/16
 */
static uint32_t UART_GetPclk2(void)
{
    uint32_t hclk = SystemCoreClock;
    uint32_t ppre = (RCC->CFGR >> 11) & 0x07u;          /* 取 PPRE2[2:0] */

    return (ppre < 0x04u) ? hclk : (hclk >> (ppre - 3u));
}

/* ============================ 初始化 ============================ */
/**
 * @brief  初始化 USART1: PA9=TX, PA10=RX, 8N1, 开启 RXNE 中断
 */
void UART_Init(uint32_t baudrate)
{
    uint32_t pclk2;

    /* ---------- ① 开时钟: IOPAEN(bit2) + USART1EN(bit14) + AFIOEN(bit0) ---------- */
    RCC->APB2ENR |= (1u << 2) | (1u << 14) | (1u << 0);

    /* ---------- ② 引脚: GPIOA->CRH ----------
     *  CRH 管 PA8~PA15, 每 4 位一个引脚: 引脚 n 占 bit[(n-8)*4+3 : (n-8)*4]
     *    PA9  -> bit7:4  = 0xB  ([CNF=10 复用推挽][MODE=11 50MHz])
     *    PA10 -> bit11:8 = 0x4  ([CNF=01 浮空输入][MODE=00 输入])
     */
    GPIOA->CRH &= ~0x00000FF0u;                       /* 先清 PA9/PA10 的 8 个配置位 */
    GPIOA->CRH |= (0x000000B0u | 0x00000400u);

    /* ---------- ③ 波特率: USART1->BRR ----------
     *  USARTDIV = PCLK2 / (16 × 波特率)
     *  BRR[15:4] = 整数部分, BRR[3:0] = 小数部分 × 16
     *  等价写法: BRR = round(PCLK2 / 波特率)  (因为 BRR 本身就是"1/16 位"的计数)
     *  72MHz / 115200 = 625 = 0x271  ->  整数 39 小数 1  ->  BRR = 39<<4 | 1
     */
    pclk2 = UART_GetPclk2();
    USART1->BRR = (uint16_t)((pclk2 + (baudrate / 2u)) / baudrate);

    /* ---------- ④ 帧格式与控制位 ----------
     *  CR1: bit13 UE=1 使能 | bit3 TE=1 发送 | bit2 RE=1 接收 | bit5 RXNEIE=1 接收中断
     *       bit12 M=0 8 位数据 | bit10 PCE=0 无校验
     *  CR2: bit13:12 STOP=00 1 个停止位
     *  CR3: 全 0 (无硬件流控, 不用 DMA)
     */
    USART1->CR2 = 0x0000u;
    USART1->CR3 = 0x0000u;
    USART1->CR1 = (1u << 13) | (1u << 5) | (1u << 3) | (1u << 2);

    /* ---------- ⑤ NVIC: CMSIS 内核函数(不需要 misc.c) ---------- */
    NVIC_SetPriority(USART1_IRQn, 2);
    NVIC_EnableIRQ(USART1_IRQn);                      /* 等价 NVIC->ISER[0] |= 1<<(IRQn&0x1F) */
}

/* ============================ 中断处理 ============================ */
/**
 * @brief  USART1 中断: 收到字节就搬进环形缓冲
 * @note   读 USART1->DR 会自动清 SR 的 RXNE 标志
 */
void UART_IRQHandler(void)
{
    if ((USART1->SR & (1u << 5)) != 0u)               /* bit5 RXNE: 收到一个字节 */
    {
        uint8_t  data = (uint8_t)(USART1->DR & 0x1FFu);   /* 读 DR(低 9 位有效) */
        uint16_t next = (uint16_t)((s_rx_head + 1u) & (UART_RX_BUF_SIZE - 1u));

        if (next == s_rx_tail)
        {
            s_rx_ovf++;                               /* 满: 丢弃 + 计数, 中断里绝不死等 */
        }
        else
        {
            s_rx_buf[s_rx_head] = data;
            s_rx_head = next;
        }
    }
}

/* ============================ 发送(阻塞等 TXE) ============================ */
void UART_SendByte(uint8_t b)
{
    while ((USART1->SR & (1u << 7)) == 0u)            /* bit7 TXE: 发送数据寄存器空 */
    {
    }
    USART1->DR = (uint16_t)b;                         /* 写 DR 发出 */
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

/* ======================================================================
 *  以下部分(环形缓冲 + 帧解析)是**纯软件逻辑**, 与标准库版完全相同 ——
 *  说明: 寄存器操作只影响"怎么和外设打交道", 而数据结构/状态机是写代码的功夫。
 * ====================================================================== */

/* ============================ 接收(环形缓冲) ============================ */
uint16_t UART_RxAvailable(void)
{
    return (uint16_t)((s_rx_head - s_rx_tail) & (UART_RX_BUF_SIZE - 1u));
}

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
 * @brief  发送一帧: 0xAA 0x55 | LEN | PAYLOAD[LEN] | SUM
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
 * @brief  解析一帧(非阻塞), 成功返回 1
 * @note   状态: 0 等0xAA -> 1 等0x55 -> 2 取LEN -> 3 收PAYLOAD -> 4 校验SUM
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
            case 0u:
                if (data == UART_FRAME_HEAD0) { state = 1u; }
                break;

            case 1u:
                if (data == UART_FRAME_HEAD1)      { state = 2u; }
                else if (data == UART_FRAME_HEAD0) { state = 1u; }
                else                               { state = 0u; }
                break;

            case 2u:
                if ((data == 0u) || (data > UART_FRAME_MAX_LEN))
                {
                    state = 0u;
                }
                else
                {
                    need  = data;
                    idx   = 0u;
                    sum   = data;
                    state = 3u;
                }
                break;

            case 3u:
                payload[idx] = data;
                idx++;
                sum = (uint8_t)(sum + data);
                if (idx >= need) { state = 4u; }
                break;

            case 4u:
                state = 0u;
                if (data == sum)
                {
                    *len = need;
                    return 1u;
                }
                break;

            default:
                state = 0u;
                break;
        }
    }
    return 0u;
}
