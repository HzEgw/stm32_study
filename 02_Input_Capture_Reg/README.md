# 02_Input_Capture_Reg —— 输入捕获（**纯寄存器版** · PWM 输入模式）

> 与 `02_Input_Capture`（标准库版）功能、引脚、现象完全一致。
> 差别：本工程**不用任何标准库 .c 文件**，全部直接写寄存器。

---

## 1. 引脚与自测（同标准库版）

| 引脚 | 方向 | 功能 |
|---|---|---|
| PA6 | 输入 | TIM3_CH1，上升沿捕获 → 周期 |
| PA7 | 输入 | TIM3_CH2，下降沿捕获 → 高电平时间 |
| PB6 | 输出 | TIM4_CH1，自测方波 1kHz / 30% |

**自测：一根跳线 PB6 → PA6**，然后看 Watch 窗口：

| 变量 | 预期 | 说明 |
|---|---|---|
| `g_freq_hz` | 1000 | 频率 |
| `g_period` / `g_high` / `g_duty` | 1000 / 300 / 300 | 周期 / 高电平 / 占空比 |
| `g_reg_ccr1` / `g_reg_ccr2` | ~1000 / ~300 | 直接读 `TIM3->CCR1/CCR2` |
| `g_reg_ccmr1` | `0x3231` | 各 4 位含义见下表 |
| `g_reg_ccer` | `0x31` | CC1E\|CC2E\|CC2P |
| `g_reg_smcr` | `0x54` | SMS=100, TS=101, MSM=1 |
| `g_reg_cnt` | 0~1000 循环 | 复位模式下一个周期就归零 |

## 2. 寄存器位域详解（本工程的核心）

**`TIM3->CCMR1 = 0x3231` 拆开看：**

| 位 | 名称 | 值 | 含义 |
|---|---|---|---|
| 1:0 | `CC1S` | 01 | IC1 直接连到 TI1（PA6） |
| 3:2 | `IC1PSC` | 00 | 每个边沿都捕获 |
| 7:4 | `IC1F` | 0011 | 滤波：连续 4 次采样一致才算边沿 |
| 9:8 | `CC2S` | 10 | IC2 间接连到 TI1（同一根线，取下降沿） |
| 11:10 | `IC2PSC` | 00 | — |
| 15:12 | `IC2F` | 0011 | 同样滤波 |

**`TIM3->CCER = 0x31`：** `CC1E(bit0)=1`、`CC1P(bit1)=0`（上升沿）、`CC2E(bit4)=1`、`CC2P(bit5)=1`（下降沿）

**`TIM3->SMCR = 0x54`：** `SMS[2:0]=100`（复位模式）、`TS[6:4]=101`（触发源 TI1FP1）、`MSM(bit7)=1`
→ 每个上升沿自动清 `CNT`，所以 `CCR1` 就是完整周期的计数

**清标志的坑：** `TIM3->SR` 是 **rc_w0**（写 0 清除），所以写成 `TIM3->SR = ~(1u<<1);`（而不是 `&= ~`）

## 3. Keil 建工程（比标准库版少 4 个文件）

1. 新建工程 → **STM32F103C8** → 关 RTE → 复制 `startup_stm32f10x_md.s`。
2. 分组：

| 分组 | 文件 |
|---|---|
| `USER` | `USER\main.c` `USER\stm32f10x_it.c` |
| `HARDWARE` | `HARDWARE\ic_reg.c` |
| `CMSIS` | `system_stm32f10x.c` + `startup_stm32f10x_md.s` |

**没有 FWLIB 分组**（不需要 `misc.c`，NVIC 用的是 CMSIS 的 `NVIC_EnableIRQ/NVIC_SetPriority`）。

3. Include Paths（4 条）：`.\USER` `.\HARDWARE` `.\Libraries\CMSIS\CM3\CoreSupport` `.\Libraries\CMSIS\DeviceSupport\ST\STM32F10x`
4. Define：**只有** `STM32F10X_MD`（不要写 `USE_STDPERIPH_DRIVER`）
5. Debug / ST-Link 设置与其它工程相同。

## 4. 标准库 ↔ 寄存器 对照（输入捕获部分）

| 标准库（`02_Input_Capture`） | 寄存器（本工程） |
|---|---|
| `TIM_PWMIConfig(TIM3, &s)` | `TIM3->CCMR1 = 0x3231; TIM3->CCER = 0x31;` |
| `TIM_SelectInputTrigger(TIM3, TIM_TS_TI1FP1)` | `TIM3->SMCR = (TIM3->SMCR & ~(7<<4)) \| (5<<4);` |
| `TIM_SelectSlaveMode(TIM3, TIM_SlaveMode_Reset)` | `TIM3->SMCR \|= (4<<0);` |
| `TIM_ITConfig(TIM3, TIM_IT_CC1\|TIM_IT_CC2, ENABLE)` | `TIM3->DIER \|= (1<<1)\|(1<<2);` |
| `NVIC_Init(&s)`（需 `misc.c`） | `NVIC_EnableIRQ(TIM3_IRQn); NVIC_SetPriority(TIM3_IRQn,1);`（CMSIS） |
| `TIM_GetCapture1(TIM3)` | `TIM3->CCR1` |
| `TIM_GetITStatus/ClearITPendingBit` | 读 `TIM3->SR` 的 `CC1IF`；写 `TIM3->SR = ~(1<<1)` |
| `SysTick_Config(72000)`（只开中断） | `SysTick->LOAD=71999; VAL=0; CTRL=(1<<2)\|(1<<1)\|(1<<0);` |

## 5. 常见问题

| 现象 | 原因 |
|---|---|
| 全 0 | 跳线没接 / 未共地；`g_reg_cnt` 是否在变化可判断定时器是否在跑 |
| 改不出 CC1IF | 忘开 `TIM3->DIER` 或 NVIC 没使能（用 `NVIC_EnableIRQ`） |
| 频率对但占空比不对 | `CC2P` 没设成 1（下降沿）；或 `CC2S` 不是 10（间接映射） |
| 编译报 `stm32f10x_conf.h` | 多写了 `USE_STDPERIPH_DRIVER`，删掉 |
