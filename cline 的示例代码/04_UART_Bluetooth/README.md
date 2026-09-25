# 04_UART_Bluetooth —— 串口通信（标准库版 · 中断接收 + 环形缓冲 + 帧协议）

> 独立工程，自带 `Libraries/`。这是 **W2 的主线工程**：串口是后面 micro-ROS / 蓝牙调参 / 上位机画曲线 的地基。
> 寄存器版见 `04_UART_Bluetooth_Reg`（本轮尚未生成）。

---

## 1. 学到的知识点

| 知识点 | 说明 |
|---|---|
| USART 初始化 | 波特率（BRR）、数据位/停止位/校验（CR1/CR2）、收发使能（CR1 的 TE/RE） |
| **中断接收** | RXNE 中断（CR1 的 RXNEIE），中断里**只搬数据**，解析放主循环 |
| **环形缓冲** | head/tail 双指针 + 2 的幂长度用 `&` 取模；满时**丢弃并计数**，绝不在中断里死等 |
| **帧协议** | `0xAA 0x55 \| LEN \| PAYLOAD \| SUM`，`SUM = (LEN + ΣPAYLOAD) & 0xFF` |
| **状态机解析** | 5 状态解析器（找头→找头→取长→收数据→校验），非阻塞、可反复调用 |
| 共地与电平 | 3.3V 电平；USB-TTL 的 TX 接 MCU 的 RX（交叉），GND 必须共地 |

## 2. 引脚

| 引脚 | 方向 | 功能 |
|---|---|---|
| PA9 | 输出 | USART1_TX |
| PA10 | 输入 | USART1_RX |
| PA13/PA14 | — | 留给 ST-Link（未占用） |

## 3. Keil 建工程

| 分组 | 文件 |
|---|---|
| `USER` | `USER\main.c` `USER\stm32f10x_it.c` |
| `HARDWARE` | `HARDWARE\uart.c` |
| `CMSIS` | `system_stm32f10x.c` + `startup_stm32f10x_md.s` |
| `FWLIB` | `misc.c`（NVIC）`stm32f10x_gpio.c` `stm32f10x_rcc.c` **`stm32f10x_usart.c`** |

Include Paths 5 条；Define `USE_STDPERIPH_DRIVER, STM32F10X_MD`；不用 MicroLIB（本工程没用到 printf）。
Debug = ST-Link / SW / Reset and Run。

## 4. 怎么验证

### 方式 A：USB-TTL 模块（最推荐）
```
USB-TTL TX  →  PA10 (MCU 的 RX)
USB-TTL RX  →  PA9  (MCU 的 TX)
GND         →  GND          ← 必须共地, 否则收到乱码
```
串口助手设 **115200 / 8 / N / 1**，HEX 显示。上电应看到：

| 时刻 | 收到内容 | 含义 |
|---|---|---|
| 上电 | `USART1 ready @115200 8N1\r\n`（文本） | 串口初始化成功 |
| 每 100ms | `AA 55 04 xx yy 03 ss` | 一帧遥测：计数器 + 回显计数 + 固定标识 + 校验和 |

`ss` 的算法：`(0x04 + xx + yy + 0x03) & 0xFF`，可以拿计算器核对 —— **能自己算出校验和，说明你真的懂这个协议了。**

往串口助手发 `AA 55 02 11 22 35`（HEX 发送），会**原样回显**，且 Watch 里 `g_rx_frames` 加 1、`g_last_p0=0x11`。

### 方式 B：一根杜邦线回环自测（没有 USB-TTL 时）
短接 **PA9 与 PA10**：自己发的帧被自己收到 → `g_rx_frames` 会持续增长（≈ `g_tx_frames`）。

### 方式 C：接到鱼香小车的 ESP32/蓝牙模块上
把 PA9/PA10 与模块的 RX/TX 交叉相接（共地），后面 ROS2 的串口桥就用这条链路。

## 5. 常见问题

| 现象 | 原因 |
|---|---|
| 全是乱码 | 波特率不对 / **没共地** / 时钟不是 72MHz（SystemInit 没跑） |
| 收不到任何数据 | TX/RX 接反了（要交叉）；或对方电平不是 3.3V |
| 偶尔丢字节、`g_rx_ovf` 增加 | 主循环太慢或缓冲太小 → 加大 `UART_RX_BUF_SIZE`（保持 2 的幂） |
| 能收到但 `g_rx_frames` 不涨 | 帧格式不对（长度/校验和）；用 HEX 逐字节核对 |
| 想发文本调试 | 用 `UART_SendString("...\r\n")`，不需要协议 |

## 6. 练习（想变强就做这几个）

1. **改成 TX 中断/DMA 发送**：现在 `UART_SendByte` 是阻塞等 TXE；改用 TXE 中断 + 发送环形缓冲，主循环再也不阻塞；
2. **加超时重传**：上位机收不到应答就重发；
3. **换波特率到 460800**：看是否仍稳定（考察时钟精度与线材）；
4. **把 `UART_PollFrame` 改写成"回调 + 缓冲区"版本**，为 micro-ROS 那种多话题协议铺路。

## 7. 下一步（W2 的收尾）

在 Linux 侧（`~/ros2_ws`）写一个 **C++ (rclcpp) 串口桥节点**：读串口 → 解析同样协议 → 发布 ROS2 话题；
再用 `ros2 topic echo` 验证。这就是 **micro-ROS 的"前身"**：先手写一遍桥，再用 micro-ROS 替代它，你就彻底懂"桥"了。
