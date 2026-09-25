# 01_PWM_Output_Reg —— PWM 输出（**纯寄存器版**）

> 与 `01_PWM_Output`（标准库版）功能完全一致，引脚一致，现象一致。
> 区别只有一个：**这里一句标准库函数都不用，全部直接读写寄存器**。
> 两个工程对照着看，你就能彻底搞懂"库函数到底帮你写了什么"。

---

## 1. 学到的知识点

| 知识点 | 寄存器 | 在本工程里的体现 |
|---|---|---|
| 外设时钟使能 | `RCC->APB1ENR` / `RCC->APB2ENR` | 不开时钟，后面所有寄存器写入都无效 |
| 引脚复用配置 | `GPIOx->CRL` / `CRH` | 4 位一个引脚：`CNF[1:0]` + `MODE[1:0]`，0xB = 复用推挽 50MHz |
| 时基 | `TIMx->PSC` / `ARR` | 频率 = 定时器时钟 ÷ ((PSC+1)×(ARR+1)) |
| 通道模式 | `TIMx->CCMR1/2` | `OCxM=110` PWM 模式1，`OCxPE=1` 预装载 |
| 输出使能/极性 | `TIMx->CCER` | `CCxE` 使能；TIM1 还有 `CC1NE` 互补使能 |
| 占空比 | `TIMx->CCR1~4` | 写它立刻（下个周期）改变占空比 |
| 启动/装载 | `TIMx->CR1` / `EGR` | `CEN` 启动、`ARPE` 预装载、`UG` 立即装载 |
| **死区/主输出** | `TIM1->BDTR` | `DTG[7:0]` 死区，`MOE(bit15)` 主输出使能 |
| 内核延时 | `SysTick->LOAD/VAL/CTRL` | 轮询 `COUNTFLAG(bit16)`，不开中断 |

## 2. 引脚（与标准库版完全相同，不占 SWD/串口）

| 引脚 | 功能 |
|---|---|
| PA6 / PA7 / PB0 / PB1 | TIM3 CH1~CH4 |
| PA8 / PB13 | TIM1 CH1 / CH1N（互补 + 1µs 死区） |
| PA13 / PA14 | 留给 ST-Link（SWD） |

## 3. 标准库 ↔ 寄存器 对照表（本工程最有价值的一页）

| 标准库写法（`01_PWM_Output`） | 等价的寄存器操作（本工程） |
|---|---|
| `RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE)` | `RCC->APB1ENR \|= (1 << 1);` |
| `RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE)` | `RCC->APB2ENR \|= (1 << 2);` |
| `GPIO_Init(GPIOA, &s)`（AF_PP / 50MHz） | `GPIOA->CRL &= ~(0xFF << 24); GPIOA->CRL \|= (0xBB << 24);` |
| `TIM_TimeBaseInit(TIM3, &s)` | `TIM3->PSC = psc; TIM3->ARR = arr;` |
| `TIM_OC1Init(TIM3, &s)`（PWM1） | `TIM3->CCMR1 \|= (6 << 4) \| (1 << 3); TIM3->CCER \|= (1 << 0);` |
| `TIM_OC1PreloadConfig(TIM3, TIM_OCPreload_Enable)` | `TIM3->CCMR1 \|= (1 << 3);` |
| `TIM_ARRPreloadConfig(TIM3, ENABLE)` | `TIM3->CR1 \|= (1 << 7);` |
| `TIM_SetCompare1(TIM3, ccr)` | `TIM3->CCR1 = ccr;` |
| `TIM_Cmd(TIM3, ENABLE)` | `TIM3->CR1 \|= (1 << 0);` |
| `TIM_GenerateEvent(TIM3, TIM_EventSource_Update)` | `TIM3->EGR \|= (1 << 0);` |
| `TIM_BDTRConfig(TIM1, &s)` | `TIM1->BDTR = dtg \| (1<<10) \| (1<<11) \| (1<<14) \| (1<<15);` |
| `TIM_CtrlPWMOutputs(TIM1, ENABLE)` | `TIM1->BDTR \|= (1 << 15);`（MOE） |

## 4. 在 Keil5 中新建工程（**比标准库版更简单：不需要任何标准库 .c**）

1. `Project → New uVision Project`，芯片选 **STM32F103C8**；RTE 窗口关闭；复制 `startup_stm32f10x_md.s` 选**是**。
2. 建 3 个分组（**没有 FWLIB 分组**）：

| 分组 | 添加的文件 |
|---|---|
| `USER` | `USER\main.c`  `USER\stm32f10x_it.c` |
| `HARDWARE` | `HARDWARE\pwm_reg.c` |
| `CMSIS` | `Libraries\CMSIS\DeviceSupport\ST\STM32F10x\system_stm32f10x.c`<br>`Libraries\CMSIS\DeviceSupport\ST\STM32F10x\startup\arm\startup_stm32f10x_md.s` |

3. `Options for Target → C/C++ → Include Paths` 只需 4 条：

```
.\USER
.\HARDWARE
.\Libraries\CMSIS\CM3\CoreSupport
.\Libraries\CMSIS\DeviceSupport\ST\STM32F10x
```

4. `Define` 只写一个（**不要**写 `USE_STDPERIPH_DRIVER`，本工程不用标准库）：

```
STM32F10X_MD
```

5. 不用 MicroLIB。下载设置与标准库版相同（ST-Link / SW / Reset and Run / Medium-density Flash 64K）。

## 5. 验证方法（与标准库版一致）

- **Watch 窗口**：除了 `g_duty_ch2` 等变量，还专门放了 `g_reg_tim3_psc`、`g_reg_tim3_ccr1`、`g_reg_tim3_ccer`、`g_reg_tim1_bdtr` —— **直接看硬件寄存器真实值**：
  - `g_reg_tim3_psc = 71`、`g_reg_tim3_arr = 999`（1kHz）
  - `g_reg_tim3_ccer = 0x1111`（4 个通道使能位都置 1）
  - `g_reg_tim1_bdtr` 高 16 位里 `0x8000` 就是 MOE
- **万用表**：PA6≈1.65V(50%)、PB0≈0.83V(25%)、PB1≈2.48V(75%)、PA7 在 0~3.3V 摆动
- **示波器**：PA8/PB13 双通道，看 1µs 死区

## 6. 常见问题

| 现象 | 原因 |
|---|---|
| 完全没输出，寄存器值却是对的 | 忘记开 `RCC->APB1ENR`/`APB2ENR` 里的时钟位 |
| PA8/PB13 没波形 | 忘记 `TIM1->BDTR` 的 `MOE(bit15)` |
| 引脚没波形但内部计数正常 | `GPIOx->CRL/CRH` 的 4 位没配成 0xB（复用推挽） |
| 改占空比要等一会才生效 | 正常：`OCxPE=1` 预装载，在周期边界更新；想立刻生效就置 `EGR` 的 `UG` |
| 编译报 `stm32f10x_conf.h not found` | 多写了 `USE_STDPERIPH_DRIVER` 宏，删掉它 |
