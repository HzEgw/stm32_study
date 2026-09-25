# 电控组第一次考核 —— 按键切换三种流水灯效果（STM32F103C8T6 + 标准库）

> 目标板：STM32F103C8T6 最小系统板 / 学习板；Keil MDK（AC5）+ STM32F10x 标准外设库
> 工程位置：`流水灯控制\led_project.uvprojx`（打开后 `Build` → `Download` 即可）
> 版本：v1.0 / 2026-09-25（重构自本人第一版，原版留档在 `_原版备份_20260925\`）

---

## 1. 考核要求 → 代码对照表

| 考核要求 | 实现位置 | 用到的外设 |
|---|---|---|
| 至少 3 个 LED | `LED.h` 的 `LED_COUNT` / `LED_On/Off` | GPIO 输出（PA1/PA2/PA3，推挽，高电平点亮） |
| 板载按键切换效果 | `KEY.c` 的 `KEY_Scan()` | GPIO 输入（PA0，上拉输入，按下为低） |
| 三个效果各用一个函数实现 | `LED.c` 的 `Effect_*_Init/Step` | — |
| ① 传统流水灯（逐个点亮） | `Effect_Flow_Init/Step` | GPIO 输出（C 语言延时控制节奏） |
| ② 呼吸灯（渐亮渐灭） | `Effect_Breathe_Init/Step` | **TIM 定时器 PWM 模式**（TIM2 三路，1kHz） |
| ③ 从左至右亮度波浪（正弦） | `Effect_Wave_Init/Step` | 同上 PWM + 正弦查表 + 三路 120° 相位差 |
| 用按键来回切换调用的函数 | `Usher\main.c` 的 `g_effects[]` 效果表 | — |

---

## 2. 硬件接线（**先确认这三件事，再上电**）

| 器件 | 引脚 | 说明 |
|---|---|---|
| LED1 / LED2 / LED3 | **PA1 / PA2 / PA3** | 依次对应 TIM2 的 CH2 / CH3 / CH4（默认复用，无需重映射）；**高电平点亮**（写 1 亮） |
| 板载按键 | **PA0** | 上拉输入；**按下读到 0**（另一端接 GND）。PA0 也是 TIM2_CH1，本工程不使用，所以互不干扰 |

> 如果你的板子接线不同：
> - LED 极性反了（低电平点亮）→ 把 `LED.c` 里 `LED_On/Off` 的 `GPIO_SetBits/ResetBits` 互换，或把 PWM 极性改成 `TIM_OCPolarity_Low`。
> - 按键按下是高电平 → 改 `KEY.c` 顶部两行：`KEY_PRESSED` 改 `1u`、`GPIO_Mode_IPU` 改 `GPIO_Mode_IPD`。
> - LED 不在 PA1/PA2/PA3 上 → 必须重新选"能出 PWM 的引脚"（TIM2 默认 CH1~CH4 = PA0/PA1/PA2/PA3，重映射后是 PA15/PB3/PB10/PB11 或 PA0~PA3），并同步改 `LED_SetBrightness()` 里的通道号。

---

## 3. 效果说明与参数（现场演示时按这个讲）

| 效果 | 现象 | 关键参数 | 想改快慢改哪 |
|---|---|---|---|
| ① 流水灯 | 单个亮点从左往右跑，200ms 换一个灯 | `FLOW_TICKS_PER_LED = 10`（× 20ms = 200ms） | `LED.c` 里 `FLOW_TICKS_PER_LED` |
| ② 呼吸灯 | 3 个灯一起渐亮→渐灭，一个来回 2.4s | 正弦表 120 点 × 20ms | `LED.h` 里 `EFFECT_STEP_MS` |
| ③ 波浪灯 | 亮峰从左往右推，三灯相位差 120° | 120/3 = 40 点 = 精确 120° | 同上 |

**PWM 参数推导（考核最容易被问的一题）**

```
TIM2 在 APB1；APB1 分频 = 2 → PCLK1 = 36MHz
F1 时钟树规定：APB 分频 ≠ 1 时，定时器时钟 = PCLK1 × 2 = 72MHz
PSC = 71  → 计数频率 = 72MHz / (71+1) = 1MHz   （1 个计数 = 1µs）
ARR = 999 → 周期 = 1000 计数 = 1000µs
PWM 频率 = 1 / 1000µs = 1kHz（正好 1000Hz）
占空比 = CCR / (ARR+1) = CCR / 1000  →  CCR 就当"千分比"用，0~1000 不用换算
```

---

## 4. 我第一版为什么"屎山 + 失败"（4 个真坑，全部已修）

| # | 症状 | 原因（对照 `_原版备份_20260925\`） | 修法 |
|---|---|---|---|
| 1 | **进波浪灯后"怎么按都出不去"** | `key_mode==3` 分支里是 `while(1)`：它把主循环吃掉了（`key_run()` 不再跑），所以 `key_mode` 永远停在 3；虽然 while 里手写读 PA0 能 `break` 出来，但回到主循环 `LED_RUN_Current(3)` 又被立刻叫一次 → **又 `PWM_INIT()` + 又进 while(1)**，等效卡死（而且 `exit_flag` 设了没人用） | 效果函数改成"走一小步就返回"（`Effect_*_Step`），主循环每 20ms 调一次 → 按键随时能被扫到，`g_mode` 正常 +1 |
| 2 | **切到 PWM 后流水灯不亮 / 状态错乱** | 引脚被 `PWM_INIT()` 配成"复用推挽（AF_PP）"后，引脚由 TIM2 的 CCR 决定，`GPIO_WriteBit` 改的 ODR 一点用没有；切回普通输出也没人管（而 CCR 里还留着上一轮的正弦垃圾值） | 每个效果的 `Init()` 里做一次自我配置：`LED_GpioInit()`（普通推挽）或 `LED_PwmInit()`（复用 + PWM）。**换效果必须重新配引脚** |
| 3 | **波浪灯亮度乱跳 / 亮度阶梯感很粗** | `50 + 50*sin(t)` 里 `sin()` 没 `#include <math.h>`（build log `#223-D` 隐式声明）→ 返回值被按 `int` 解释，实际拿到的是 double 的低 32 位 = 垃圾数；而且 `ccr2` 在内外层重复定义、CCR 只有 0~100 个台阶（ARR=99） | ① 改用 **120 点正弦查表**（M3 没有 FPU，查表更快更准，还省数学库）；② `ARR=999` → 亮度 1000 个台阶，过渡平滑 |
| 4 | **按键"按一下跳两个"/呼吸灯在等松手时卡住** | `Delay_ms(30)` + `while(...==0);` 阻塞等松手，等的时候灯效整个停住，而且抖动容易连报 | 改成**状态消抖**：连续 2 次（40ms）采样一致才认电平变化；按下瞬间报一次事件，按住不重复报，全程不阻塞 |

另外顺手清掉的编译警告（原版 23 个 warning）：

- `GPIO_WriteBit(GPIOA, GPIO_Pin_3, 0)` → 第三个参数是枚举 `BitAction`，写 `0/1` 会报 `#188-D`；
  新版统一用 `GPIO_SetBits()/GPIO_ResetBits()`，语义清楚且零警告。
- `uint8_t exit_flag = 0;` 定义了却没用（`#550-D`）→ 删掉。
- `LED.c` 里参数名拼成了 `key_mdoe`（和全局 `key_mode` 只差一个字母），
  导致同一个函数里"一半用参数、一半用全局变量"——这种错最难查，**变量名不要靠肉眼辨**，现在参数和状态各叫各的名字。

---

## 5. 怎么验证它真的对（三处证据）

1. **编译**：Keil 里 `Rebuild`，底部应是 `0 Error(s), 0 Warning(s)`（原版是 23 warnings）。
2. **下载看现象**：上电默认进效果①流水灯；
   按一下 → 呼吸灯；再按 → 波浪灯；再按 → 回到流水灯（`g_mode` 0→1→2→0）。
3. **Debug 看数据**（Watch 窗口加这几个变量，全部 `volatile`，不会被优化掉）：

| Watch 变量 | 期望值 |
|---|---|
| `g_mode` | 0 / 1 / 2，每按一次按键 +1（到 2 后回 0） |
| `g_mode_name` | 展开指针可看到当前效果名，如 `"1-Flow(流水灯)"` |
| `g_key_count` | 按一次 +1 |
| `g_led1_duty ~ g_led3_duty` | 效果②三路同步变化；效果③三路相差 40 个点（120°）；效果①恒为 0（GPIO 模式） |
| `TIM2->CCR2` | 直接看寄存器，与 `g_led1_duty` 一致 |

---

## 6. 还可以怎么加分（答辩前有空再挑一个做）

1. **改成"全亮点、逐个熄灭"**的传统流水灯：`Effect_Flow_Step` 里把 `LED_AllOff()` 换成"全亮"，再灭掉当前那个。
2. **波浪反向**：把 `Effect_Wave_Step` 里 `i2` / `i3` 的两条延迟量对调即可（`SIN_PHASE` ↔ `SIN_LEN - SIN_PHASE`），亮峰就从右往左跑。
3. **长按急停**：`KEY_Scan()` 再返回一个"长按"事件（连续 N 次都读到按下），主循环里切到"全灭"效果。
4. **拆文件**：把三个 `Effect_*` 挪到新建的 `Hardware\Effect.c/h`，Keil 里 `Add Existing Files` 加进 Hardware 分组即可（本版故意放在 `LED.c` 里，省得动工程配置）。
5. **换成"互斥"效果**：呼吸灯三灯反向（`s_sin[i]` 与 `s_sin[(i+60)%120]`），一升一降更好看。

---

## 7. 文件清单

```
流水灯控制\
├── Hardware\
│   ├── LED.c / LED.h     # 3 个 LED 的底层驱动 + 三种效果（流水/呼吸/波浪）+ 正弦表
│   ├── PWM.c / PWM.h     # TIM2 三路 PWM（PA1/PA2/PA3，1kHz，千分比占空比）
│   └── KEY.c / KEY.h     # 按键 PA0 非阻塞消抖扫描
├── Usher\
│   ├── main.c            # 主循环 + 效果表（g_effects[]，按键切换）
│   ├── stm32f10x_it.c    # 空的中断服务函数（本工程不靠中断）
│   └── stm32f10x_conf.h  # 标准库总配置（已包含 gpio/tim/rcc/misc）
├── System\Delay.c/.h     # SysTick 轮询式延时（Delay_ms 给主循环定 20ms 节奏）
├── Start\ Library\       # CMSIS + 标准外设库（不改）
└── _原版备份_20260925\   # 第一版原始代码（学习对照用，不参与编译）
```

> 中文注释若在 Keil 里显示乱码：`Edit → Configuration → Editor → Encoding` 选 **UTF-8**（本工程注释按 UTF-8 保存）。
