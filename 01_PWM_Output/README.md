# 01_PWM_Output —— PWM 输出（TIM3 四路 + TIM1 互补带死区）

> 本工程是 `STM32F103C8_Workspace` 多根工作空间里的独立工程之一，
> 自带 `Libraries/`，可单独打开、单独编译、单独拷走。
> Keil 工程由你自己新建（下面第 3 节照着点即可），代码已经写好。

---

## 1. 学到的知识点

| 知识点 | 在本工程里的体现 |
|---|---|
| 通用定时器 PWM | TIM3，4 个独立通道，同一周期、占空比各自可调 |
| 高级定时器互补输出 | TIM1，CH1(PA8) / CH1N(PB13) 天然互补 |
| **死区时间** | `TIM_BDTRConfig()` + 纳秒→DTG 四段公式换算，用示波器能量出来 |
| 主输出使能 MOE | `TIM_CtrlPWMOutputs()`，不懂这条就没有输出（高级定时器独有的坑） |
| 预装载 / 影子寄存器 | ARR、CCR 全部开预装载，运行中改参数不产生畸形波形 |
| 整数占空比换算 | 千分比(0~1000) → CCR，全整数运算，不使用浮点 |

## 2. 引脚分配（不占用 SWD 与串口，ST-Link 可随时下载）

| 引脚 | 功能 | 备注 |
|---|---|---|
| PA6 | TIM3_CH1 | 固定 50% 占空比 |
| PA7 | TIM3_CH2 | 呼吸(0→100%→0) |
| PB0 | TIM3_CH3 | 固定 25% |
| PB1 | TIM3_CH4 | 固定 75% |
| PA8 | TIM1_CH1 | 与 PB13 互补 |
| PB13 | TIM1_CH1N | 互补输出，死区 1µs |
| PA13 / PA14 | SWDIO / SWCLK | 保留给 ST-Link，**不要**占用 |

## 3. 在 Keil5 中新建工程（照抄即可）

1. `Project → New uVision Project`，芯片选 **STM32F103C8**；弹出的 RTE 窗口**直接关闭**；
   问是否复制 `startup_stm32f10x_md.s` 时选**是**。
2. `Manage Project Items` 建 4 个分组，添加这些文件：

| 分组 | 添加的文件 |
|---|---|
| `USER` | `USER\main.c`  `USER\stm32f10x_it.c` |
| `HARDWARE` | `HARDWARE\pwm.c` |
| `CMSIS` | `Libraries\CMSIS\DeviceSupport\ST\STM32F10x\system_stm32f10x.c`<br>`Libraries\CMSIS\DeviceSupport\ST\STM32F10x\startup\arm\startup_stm32f10x_md.s` |
| `FWLIB` | `Libraries\STM32F10x_StdPeriph_Driver\src\` 下的<br>`misc.c` `stm32f10x_gpio.c` `stm32f10x_rcc.c` `stm32f10x_tim.c`（本工程只要这 4 个） |

3. `Options for Target → C/C++ → Include Paths` 添加 5 条：

```
.\USER
.\HARDWARE
.\Libraries\CMSIS\CM3\CoreSupport
.\Libraries\CMSIS\DeviceSupport\ST\STM32F10x
.\Libraries\STM32F10x_StdPeriph_Driver\inc
```

4. 同一个选项卡的 `Define` 填：

```
USE_STDPERIPH_DRIVER, STM32F10X_MD
```

> `STM32F10X_MD` 漏掉会直接报
> `#error "Please select first the target STM32F10x device used in your application"`。

5. **不需要** 勾选 MicroLIB（本工程没有 printf）。
6. 下载设置：`Options for Target → Debug` 选 **ST-Link Debugger** → `Settings`
   - `Debug` 页：Port 选 **SW**，能看到 Device；
   - `Flash Download` 页：勾选 **Reset and Run**，Programming Algorithm 选
     **STM32F10x Medium-density Flash 64K**。
7. 接线：ST-Link 的 `SWCLK→PA14`、`SWDIO→PA13`、`GND→GND`、`3.3V→3.3V`。
8. `F7` 编译，`F8` 下载。

## 4. 怎么验证（没有 LED、没有串口也行）

| 方法 | 怎么做 | 预期现象 |
|---|---|---|
| **Keil Watch 窗口** | 调试模式下把 `g_duty_ch2`、`g_tim3_freq`、`g_tim1_dead` 拖进 Watch | 占空比 0↔1000 来回变；频率 1000；死区约 1000(ns) |
| **万用表（直流电压档）** | 黑表笔 GND，红表笔分别量 PA6/PA7/PB0/PB1 | PA6≈1.65V(50%)，PB0≈0.83V(25%)，PB1≈2.48V(75%)，PA7 在 0~3.3V 之间摆动 |
| **示波器/逻辑分析仪** | CH1 接 PA8、CH2 接 PB13 | 两路互补，切换瞬间有明显的 **1µs 死区**（两路同时为低） |

> 原理：方波的平均电压 = 3.3V × 占空比，所以普通万用表也能"看"占空比。

## 5. 代码结构

```
HARDWARE/pwm.h   对外接口(含参数宏)
HARDWARE/pwm.c   驱动实现
  ├─ PWM_GetTimerClock()    实时算定时器时钟(不写死 72MHz)
  ├─ PWM_CalcPscArr()       频率 → PSC/ARR
  ├─ PWM_DeadTimeFromNs()   纳秒 → DTG(按 RM0008 四段公式)
  ├─ PWM_Init() / PWM_SetFreq() / PWM_SetDuty() / PWM_GetDuty()
  └─ PWM_AdvInit() / PWM_AdvSetDuty() / PWM_AdvGetDeadTimeNs() / PWM_AdvEnable()
USER/main.c      初始化 + 呼
USER/stm32f10x_it.c  本工程不用中断, 只保留内核异常空实现
```

## 6. 常见问题

| 现象 | 原因 |
|---|---|
| 高级定时器 PA8/PB13 没输出 | 忘了 `TIM_CtrlPWMOutputs(TIM1, ENABLE)`（MOE） |
| 编译报 `Please select first the target...` | Keil 里没写 `STM32F10X_MD` |
| 找不到 `stm32f10x.h` | Include Paths 少了 `Libraries` 那几条 |
| 频率不是 1000Hz | 用 `PWM_GetActualFreq()` 看实际值；72MHz 下 1kHz 是整除的，应该精确等于 1000 |
| 万用表读数一直不变 | 量错引脚（TIM3 默认复用就是 PA6/PA7/PB0/PB1，无需重映射）；或忘记启动 `TIM_Cmd` |
| 想整体关掉互补输出（急停） | 调 `PWM_AdvEnable(DISABLE)` |

## 7. 想看"寄存器版"对照？

同一工作空间下的 **`01_PWM_Output_Reg`** 是功能完全相同、但**全部直接操作寄存器**的版本，
它的 README 第 3 节有一张"标准库 ↔ 寄存器"的逐条对照表，建议两个工程**左右分屏对照**看。

本工程的 `pwm.c` 里，每条标准库函数后面也补了注释，写清它实际动了哪个寄存器，例如：

| 标准库写法 | 实际寄存器操作 |
|---|---|
| `RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE)` | `RCC->APB1ENR \|= (1 << 1);` （TIM3EN） |
| `GPIO_Init(GPIOA, &s)`（AF_PP 50MHz） | `GPIOA->CRL &= ~(0xFF << 24); GPIOA->CRL \|= (0xBB << 24);` |
| `TIM_TimeBaseInit(TIM3, &s)` | `TIM3->PSC = psc; TIM3->ARR = arr;` |
| `TIM_OC1Init(TIM3, &s)` | `TIM3->CCMR1 \|= (6 << 4) \| (1 << 3); TIM3->CCER \|= (1 << 0);` |
| `TIM_SetCompare1(TIM3, ccr)` | `TIM3->CCR1 = ccr;` |
| `TIM_Cmd(TIM3, ENABLE)` | `TIM3->CR1 \|= (1 << 0);` |
| `TIM_GenerateEvent(TIM3, TIM_EventSource_Update)` | `TIM3->EGR \|= (1 << 0);` |
| `TIM_CtrlPWMOutputs(TIM1, ENABLE)` | `TIM1->BDTR \|= (1 << 15);` （MOE） |
