# 02_Input_Capture —— 输入捕获（标准库版 · PWM 输入模式测频率+占空比）

> 独立工程，自带 `Libraries/`。Keil 工程由你新建，代码已写好。
> 寄存器版见同工作空间的 `02_Input_Capture_Reg`。

---

## 1. 学到的知识点

| 知识点 | 说明 |
|---|---|
| **PWM 输入模式** | TIM3_CH1(PA6) 捕上升沿得周期，TIM3_CH2(PA7) 捕下降沿得高电平时间，**一次测出频率和占空比** |
| 从模式"复位" | `SMS=100`：每个上升沿自动把 `CNT` 清零 → `CCR1` 直接就是"一个周期的计数" |
| 触发源选择 | `TS=101`（TI1FP1），把 CH1 的滤波后信号当作触发源 |
| **输入滤波** | `IC1F=0x03`：连续 4 次采样一致才认一个边沿，抑制抖动/毛刺 |
| 捕获中断 | `CC1IF` / `CC2IF`，读 `CCR1`/`CCR2` 取数据 |
| **无信号超时** | SysTick 1ms 累加，100ms 没捕获 → 判 0Hz（工业代码必备的"丢信号保护"） |
| 量程设计 | 1MHz 计数（1µs 分辨率）+ ARR=65535 → 最低可测约 15Hz；高频受中断开销限制 |

## 2. 引脚

| 引脚 | 方向 | 功能 |
|---|---|---|
| PA6 | 输入 | TIM3_CH1，上升沿捕获（周期） |
| PA7 | 输入 | TIM3_CH2，下降沿捕获（高电平） |
| **PB6** | **输出** | TIM4_CH1，自测方波 1kHz / 30%（一根跳线 PB6→PA6 就能自测） |
| PA13/PA14 | — | 留给 ST-Link，未占用 |

## 3. Keil 建工程

1. 新建工程 → 芯片 **STM32F103C8** → 关闭 RTE → 复制 `startup_stm32f10x_md.s` 选**是**。
2. 分组与文件：

| 分组 | 文件 |
|---|---|
| `USER` | `USER\main.c` `USER\stm32f10x_it.c` |
| `HARDWARE` | `HARDWARE\ic.c` |
| `CMSIS` | `Libraries\CMSIS\DeviceSupport\ST\STM32F10x\system_stm32f10x.c`<br>`Libraries\CMSIS\DeviceSupport\ST\STM32F10x\startup\arm\startup_stm32f10x_md.s` |
| `FWLIB` | `misc.c`（NVIC）`stm32f10x_gpio.c` `stm32f10x_rcc.c` `stm32f10x_tim.c` |

3. Include Paths（5 条）：`.\USER` `.\HARDWARE` `.\Libraries\CMSIS\CM3\CoreSupport` `.\Libraries\CMSIS\DeviceSupport\ST\STM32F10x` `.\Libraries\STM32F10x_StdPeriph_Driver\inc`
4. Define：`USE_STDPERIPH_DRIVER, STM32F10X_MD`
5. 不用 MicroLIB；Debug 选 ST-Link / SW / Reset and Run / Medium-density Flash 64K。

## 4. 怎么验证（不需要 LED、不需要串口）

1. **接一根跳线：PB6 → PA6**（PB6 是本工程自己发出的 1kHz/30% 方波）。
2. 下载后进调试模式，把变量拖进 **Watch 窗口**：

| 变量 | 自测时的预期值 | 含义 |
|---|---|---|
| `g_freq_hz` | **1000** | 频率 1kHz |
| `g_period` | **1000** | 周期 1000µs |
| `g_high` | **300** | 高电平 300µs |
| `g_duty` | **300** | 占空比 30.0% |
| `g_captures` | 持续增大 | 捕获在持续发生 |
| `g_lost` | **0** | 有信号 |
| `g_ccr1_reg` | 约 **1000** | 直接读 `TIM3->CCR1`，硬件真实值 |

3. **拔掉跳线** → 约 100ms 后 `g_lost` 变 **1**、`g_freq_hz` 变 **0**：超时保护生效。
4. 想测外部信号：信号发生器的地要与板子共地，信号接到 PA6；改变占空比/频率，Watch 里的值会跟着变。

## 5. 关键库函数 → 寄存器对照

| 标准库写法 | 实际寄存器操作 |
|---|---|
| `TIM_PWMIConfig(TIM3, &s)` | `TIM3->CCMR1` 的 `CC1S=01`、`IC1F`、`CC2S=10`；`TIM3->CCER` 的 `CC1E/CC1P/CC2E/CC2P` |
| `TIM_SelectInputTrigger(TIM3, TIM_TS_TI1FP1)` | `TIM3->SMCR` 的 `TS[6:4] = 101` |
| `TIM_SelectSlaveMode(TIM3, TIM_SlaveMode_Reset)` | `TIM3->SMCR` 的 `SMS[2:0] = 100` |
| `TIM_SelectMasterSlaveMode(TIM3, TIM_MasterSlaveMode_Enable)` | `TIM3->SMCR` 的 `MSM(bit7) = 1` |
| `TIM_ITConfig(TIM3, TIM_IT_CC1 \| TIM_IT_CC2, ENABLE)` | `TIM3->DIER \|= (1<<1) \| (1<<2)` |
| `TIM_GetCapture1(TIM3)` | 读 `TIM3->CCR1` |
| `TIM_GetITStatus(TIM3, TIM_IT_CC1)` | 读 `TIM3->SR` 的 `CC1IF(bit1)` |
| `TIM_ClearITPendingBit(TIM3, TIM_IT_CC1)` | 写 `TIM3->SR` 的 `CC1IF = 0` |
| `SysTick_Config(72000)` | `SysTick->LOAD=71999; VAL=0; CTRL=(1<<2)\|(1<<1)\|(1<<0)` |

## 6. 常见问题

| 现象 | 原因 |
|---|---|
| 全是 0，`g_captures` 也不动 | 跳线没接 / 没共地 / 信号没进 PA6；先确认 PB6 真的在输出 |
| `g_lost = 1` 一直不变 | 同上，没捕获到任何边沿 |
| 读数在小范围抖动(±1µs) | 正常：被测信号与定时器时钟不同步，属量化误差；加大 `IC_IC_FILTER` 可抑制毛刺 |
| 高频测量误差变大 | 中断太频繁；可降低 `IC_TIMER_PSC`？不行——应改用"捕获+DMA"或降低 PSC 提高分辨率权衡 |
| 低频测不到（<15Hz） | ARR 最大 65535µs，超过就溢出；需要用"溢出计数"扩展量程（03 之后可加练） |
| 编译报找不到 `ic.h` | Include Paths 少了 `.\HARDWARE` |
