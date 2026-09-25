# 04_UART_Bluetooth_Reg —— 串口通信（**纯寄存器版**）

> 与 `04_UART_Bluetooth`（标准库版）功能、帧格式、现象完全一致；本工程**不用任何标准库 .c 文件**。

---

## 1. 初始化一共就 6 个寄存器

```c
RCC->APB2ENR |= (1u<<2) | (1u<<14) | (1u<<0);      /* IOPAEN, USART1EN, AFIOEN */
GPIOA->CRH &= ~0x00000FF0u;                        /* 清 PA9/PA10 的 8 个配置位 */
GPIOA->CRH |= (0x000000B0u | 0x00000400u);         /* PA9=0xB 复用推挽, PA10=0x4 浮空输入 */
USART1->BRR  = (uint16_t)((PCLK2 + baudrate/2) / baudrate);   /* 72MHz/115200 = 625 = 0x271 */
USART1->CR2  = 0x0000;                             /* STOP[13:12]=00 -> 1 个停止位 */
USART1->CR3  = 0x0000;                             /* 无流控, 不用 DMA */
USART1->CR1  = (1u<<13)|(1u<<5)|(1u<<3)|(1u<<2);   /* UE | RXNEIE | TE | RE */
NVIC_SetPriority(USART1_IRQn, 2);
NVIC_EnableIRQ(USART1_IRQn);                       /* 等价 NVIC->ISER[0] |= 1<<(IRQn & 0x1F) */
```

**关键位含义**

| 寄存器 | 位 | 含义 |
|---|---|---|
| `GPIOA->CRH` | PA9 = bit7:4 = 0xB | `CNF=10` 复用推挽 + `MODE=11` 50MHz |
| | PA10 = bit11:8 = 0x4 | `CNF=01` 浮空输入 + `MODE=00` 输入 |
| `USART1->BRR` | 全部 16 位 | `USARTDIV = PCLK2/(16×波特率)`，整数在高 12 位、1/16 小数在低 4 位 → 等价于 `round(PCLK2/波特率)` |
| `USART1->CR1` | bit13 `UE` | 串口总使能（不置它，发不出也收不到） |
| | bit5 `RXNEIE` | 收到数据就中断 |
| | bit3 `TE` / bit2 `RE` | 发送/接收使能 |
| `USART1->SR` | bit5 `RXNE` | 收到一个字节（**读 DR 自动清**） |
| | bit7 `TXE` | 发送数据寄存器空，可以写下一个字节 |
| `USART1->DR` | — | 读 = 收到的字节；写 = 要发的字节 |

> 环形缓冲与帧解析状态机**与标准库版完全逐行相同**——因为那是纯软件逻辑，寄存器只影响"怎么和外设打交道"。

## 2. Keil 建工程（无 FWLIB）

| 分组 | 文件 |
|---|---|
| `USER` | `USER\main.c` `USER\stm32f10x_it.c` |
| `HARDWARE` | `HARDWARE\uart_reg.c` |
| `CMSIS` | `system_stm32f10x.c` + `startup_stm32f10x_md.s` |

Include Paths 4 条；Define **只有** `STM32F10X_MD`（不要 `USE_STDPERIPH_DRIVER`）。
> 注意：本工程不需要 `misc.c`，NVIC 用的是 CMSIS 的 `NVIC_EnableIRQ / NVIC_SetPriority`。

## 3. 验证

与标准库版完全相同：

| 方式 | 操作 | 预期 |
|---|---|---|
| USB-TTL | `TX→PA10, RX→PA9, GND→GND`，串口助手 115200/8/N/1 HEX | 每 100ms 一帧 `AA 55 04 xx yy 03 ss`；发 `AA 55 02 11 22 35` 会回显 |
| 回环自测 | 短接 PA9–PA10 | `g_rx_frames` 与 `g_tx_frames` 同步增长 |
| 接鱼香小车模块 | PA9/PA10 与模块 TX/RX 交叉、共地 | 后面接 ROS2 桥用这条链路 |

**Watch 里的寄存器快照**（可直接核对初始化对不对）：

| 变量 | 期望值 | 说明 |
|---|---|---|
| `g_reg_brr` | `625` (0x271) | 72MHz / 115200 |
| `g_reg_cr1` | `0x202C` | UE(0x2000) + RXNEIE(0x20) + TE(0x8) + RE(0x4) |
| `g_reg_crh` | `0x4B0` | PA9 的 0xB0 + PA10 的 0x400 |

## 4. 常见问题

| 现象 | 原因 |
|---|---|
| 完全没反应 | 忘了 `USART1->CR1` 的 `UE(bit13)` |
| 能发不能收 | `RE(bit2)` 没置，或 `RXNEIE(bit5)` 没开，或 NVIC 没使能 |
| 乱码 | `BRR` 算错（检查 PCLK2）；或对方波特率/共地问题 |
| 收到字节但缓冲溢出 | 主循环处理太慢 → 加大 `UART_RX_BUF_SIZE`（保持 2 的幂） |
| 编译报 `stm32f10x_conf.h` 找不到 | 多写了 `USE_STDPERIPH_DRIVER`，删掉它 |

## 5. 练习

1. 把 `UART_SendByte` 改成 **TXE 中断发送**（`CR1` 的 `TXEIE(bit7)`），主循环不再阻塞；
2. 打开 **DMA 发送**（`CR3` 的 `DMAT(bit7)`）+ `DMA1_Channel4`，一次发一整帧；
3. 加**接收超时**：用 SysTick 检测"半个帧卡住"，超时丢弃重来（更健壮）；
4. 对照标准库版，把 `USART_Init()` 内部做的事逐条写在注释里（理解库函数）。
