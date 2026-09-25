# 03_Encoder_Reg —— 编码器接口（**纯寄存器版**）

> 与 `03_Encoder`（标准库版）功能、引脚、现象完全一致；本工程不使用任何标准库 .c 文件。
> 共用说明（接线、验证现象、常见问题）见 `03_Encoder\README.md`，这里只讲**寄存器相关**。

---

## 1. 编码器模式一共就动了 6 个寄存器

```c
RCC->APB2ENR |= (1u<<0) | (1u<<2);        /* AFIOEN, IOPAEN */
RCC->APB1ENR |= (1u<<1);                  /* TIM3EN */

GPIOA->CRL &= ~0xFF000000u;               /* PA6/PA7 */
GPIOA->CRL |=  0x88000000u;               /* 每引脚 4 位: CNF=10 上下拉输入, MODE=00 -> 0x8 */
GPIOA->ODR |= (1u<<6) | (1u<<7);          /* ODR=1 -> 选上拉(输入模式下 ODR 决定上/下拉) */

TIM3->PSC = 0;        TIM3->ARR = 0xFFFF; /* 72MHz 计数, 分辨率最高 */
TIM3->CCMR1 = 0xF1F1;                     /* CC1S=01 CC2S=01 直接输入; IC1F=IC2F=1111 最强滤波 */
TIM3->CCER  = (1u<<0) | (1u<<4);          /* CC1E / CC2E 输入使能, 极性 0(上升) */
TIM3->SMCR  = (TIM3->SMCR & ~0x7u) | 0x3u;/* SMS=011 编码器模式3(TI12 四倍频) */
TIM3->CNT   = 0;      TIM3->CR1 |= (1u<<0);/* 清位置, CEN=1 启动 */
```

**`CCMR1 = 0xF1F1` 拆开：**

| 位 | 名称 | 值 | 含义 |
|---|---|---|---|
| 1:0 | `CC1S` | 01 | IC1 直接连 TI1（A 相） |
| 7:4 | `IC1F` | 1111 | 最强输入滤波（抗机械抖动） |
| 9:8 | `CC2S` | 01 | IC2 直接连 TI2（B 相） |
| 15:12 | `IC2F` | 1111 | 同上 |

**`SMCR` 的编码器模式选择：** `SMS[2:0] = 001`（只数 TI2 边沿，2 倍频）、`010`（只数 TI1，2 倍频）、**`011`（TI12 两相双边沿，4 倍频，本工程）**

**读方向：** `TIM3->CR1` 的 `DIR(bit4)`，硬件自己判的（标准库没有对应函数，所以标准库版也是读这个位）

## 2. Keil 建工程（无 FWLIB）

| 分组 | 文件 |
|---|---|
| `USER` | `USER\main.c` `USER\stm32f10x_it.c` |
| `HARDWARE` | `HARDWARE\encoder_reg.c` |
| `CMSIS` | `system_stm32f10x.c` + `startup_stm32f10x_md.s` |

Include Paths 4 条；Define **只有** `STM32F10X_MD`（不要 `USE_STDPERIPH_DRIVER`）。

## 3. Watch 窗口里的"硬件真值"

| 变量 | 预期 |
|---|---|
| `g_reg_smcr` | `SMS` 位应为 `011`（低 3 位 = 3） |
| `g_reg_ccmr1` | **0xF1F1** |
| `g_reg_cnt` | 转动时连续变化；正转增加、反转减小 |
| `g_reg_cr1` | bit0（CEN）=1；bit4（DIR）随转向翻转 |
| `g_position` / `g_rpm` / `g_pwm_duty` | 与标准库版完全一致 |

## 4. 标准库 ↔ 寄存器 对照

| 标准库（`03_Encoder`） | 寄存器（本工程） |
|---|---|
| `TIM_EncoderInterfaceConfig(TIM3, TIM_EncoderMode_TI12, Rising, Rising)` | `TIM3->CCMR1=0xF1F1; TIM3->CCER=0x11; TIM3->SMCR 低 3 位=011;` |
| `TIM_ICInit(...)`（滤波） | `CCMR1` 的 `IC1F/IC2F` 位域 |
| `GPIO_Mode_IPU` | `CRL` 的 `CNF=10` + `ODR` 对应位置 1 |
| `TIM_GetCounter(TIM3)` | `TIM3->CNT` |
| `TIM_SetCounter(TIM3, 0)` | `TIM3->CNT = 0` |
| `TIM_Cmd(TIM3, ENABLE)` | `TIM3->CR1 \|= (1<<0)` |
| `TIM_SetCompare1(TIM4, ccr)` | `TIM4->CCR1 = ccr` |
| `SysTick_Config(72000)` | `SysTick->LOAD=71999; VAL=0; CTRL=(1<<2)\|(1<<1)\|(1<<0)` |
