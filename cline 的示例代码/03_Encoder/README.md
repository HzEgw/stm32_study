# 03_Encoder —— 编码器接口（标准库版 · 硬件四倍频 + 定时采样）

> 独立工程，自带 `Libraries/`。寄存器版见 `03_Encoder_Reg`。

---

## 1. 学到的知识点

| 知识点 | 说明 |
|---|---|
| **编码器接口模式** | `TIM_EncoderMode_TI12`：A/B 两相的**上升+下降沿都计数** → 四倍频；方向由硬件自动判定，**CPU 零参与、零中断、不丢脉冲** |
| 硬件判方向 | 直接读 `TIM3->CR1` 的 `DIR` 位就知道正转/反转（`Encoder_GetDirection()`） |
| 计数分辨率 | 每转计数 = `PPR × 4 × 减速比`（500 线直连 → 2000 计数/转） |
| **16→32 位位置扩展** | `delta = (int16_t)(now - last)`：用有符号差值规避 `CNT` 溢出，再累加成 32 位位置 |
| 定时采样 | SysTick 1ms 打节拍，每 50ms 置标志；**中断只置标志，除法放主循环** |
| 输入滤波 | `ENC_IC_FILTER = 0x0F`：机械编码器最容易抖动，最强滤波更稳 |
| 上拉输入 | 多数编码器是集电极开路输出，配 `GPIO_Mode_IPU` |
| 转速"可视化" | PB6 输出**与转速成正比的 PWM** → 万用表直流档就能看转速 |

## 2. 引脚

| 引脚 | 方向 | 功能 |
|---|---|---|
| PA6 | 输入(上拉) | TIM3_CH1 = 编码器 A 相 |
| PA7 | 输入(上拉) | TIM3_CH2 = 编码器 B 相 |
| PB6 | 输出 | TIM4_CH1，转速指示 PWM（1kHz，占空比 ∝ 转速，满量程 300rpm） |
| PA13/PA14 | — | ST-Link，未占用 |

## 3. Keil 建工程

| 分组 | 文件 |
|---|---|
| `USER` | `USER\main.c` `USER\stm32f10x_it.c` |
| `HARDWARE` | `HARDWARE\encoder.c` |
| `CMSIS` | `system_stm32f10x.c` + `startup_stm32f10x_md.s` |
| `FWLIB` | `misc.c`（NVIC）`stm32f10x_gpio.c` `stm32f10x_rcc.c` `stm32f10x_tim.c` |

Include Paths 5 条、Define `USE_STDPERIPH_DRIVER, STM32F10X_MD`、Debug = ST-Link/SW（与 01、02 工程完全一样）。

## 4. 验证（不需要 LED / 串口）

| 变量 | 现象 |
|---|---|
| `g_position` | 正转增大、反转减小（换向后再转回来会回到原值 → 证明四倍频计数正确） |
| `g_delta` | 转得越快越大；停止时为 0 |
| `g_rpm` | 带符号转速，公式 `rpm = 增量 / 每转计数 × (60000 / 50ms)` |
| `g_dir` | 0 正转 / 1 反转（硬件 DIR 位） |
| `g_pwm_duty` / PB6 电压 | 转速越高占空比越大，**万用表直流档量 PB6：0~3.3V 随转速变化** |
| `g_reg_cnt` / `g_reg_smcr` | 硬件真值：`CNT` 连续变化，`SMCR` 应在 `SMS=011` 附近 |

**没有编码器也能试**：用杜邦线快速碰一下 PA6(或 PA7) 到 GND/3.3V，`g_position` 会增减、`g_delta` 出现非零值。
（说明：真实编码器两相有 90° 相位差，手碰只能造出"半个脉冲"，属正常。）

## 5. 关键库函数 → 寄存器对照

| 标准库 | 实际寄存器 |
|---|---|
| `TIM_EncoderInterfaceConfig(TIM3, TIM_EncoderMode_TI12, Rising, Rising)` | `TIM3->CCMR1` 的 `CC1S=01 CC2S=01`；`TIM3->CCER` 的 `CC1E/CC2E`；`TIM3->SMCR` 的 `SMS[2:0]=011` |
| `TIM_ICInit(...)`（滤波） | `TIM3->CCMR1` 的 `IC1F[7:4]` / `IC2F[15:12]` |
| `TIM_GetCounter(TIM3)` | 读 `TIM3->CNT` |
| `TIM_SetCounter(TIM3, 0)` | 写 `TIM3->CNT = 0` |
| `Encoder_GetDirection()` | 读 `TIM3->CR1` 的 `DIR(bit4)` |
| `GPIO_Mode_IPU` | `GPIOA->CRL` 的 `CNF=10 + MODE=00`，且 `GPIOA->ODR` 对应位 = 1（选上拉） |
| `SysTick_Config(72000)` | `SysTick->LOAD=71999; VAL=0; CTRL=(1<<2)\|(1<<1)\|(1<<0)` |

## 6. 常见问题

| 现象 | 原因 |
|---|---|
| `g_position` 完全不动 | A/B 相没接对 / 没共地 / 编码器没供电；先看 `g_reg_cnt` 是否变化 |
| 转一点点就跳很多 | 正常：四倍频 + 你的 `ENC_PPR` 设置与实际编码器不符（改成真实线数） |
| 数值抖动大 | 机械编码器抖动；`ENC_IC_FILTER` 已是 0x0F（最强），再就是硬件加 RC/施密特 |
| `g_rpm` 总是 0 而 `g_position` 在动 | `Encoder_Update()` 没在主循环里调用（或 `ENC_SAMPLE_MS` 太大） |
| 转速方向相反 | 交换 A/B 两相接线，或把 B 相极性改为 `TIM_ICPolarity_Falling` |
| 位置想清零 | 调 `Encoder_Reset()` |
