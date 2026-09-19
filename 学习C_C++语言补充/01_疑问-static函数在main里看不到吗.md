# 01 · 疑问：static 定义在别的 .c 里的函数，我在 main 里看不到吗？

> **我原来的问题（原话）**
> "你说的'本 .c'其实我还没有怎么明白，难道我在 main 里面看不到 static 定义的函数吗？"

**答案：看不到，而且用不了 —— 但"看不到"分两层含义，下面全部讲清楚。**

---

## 1. 编译器眼里的"一个 .c 文件"

我们说的"一个 .c 文件"，编译器叫它**翻译单元（translation unit）**：

```
encoder.c  +  它 #include 进来的所有头文件（stm32f10x.h、encoder.h …）
        │  ①预处理：把 #include 的内容"原样文本粘贴"进来
        ▼
   一大坨完整的 C 代码
        │  ②编译
        ▼
   encoder.o   ← 独立的目标文件，里面有一张"符号表"
        │
main.o ─┼─ ③链接 ──► xxx.axf（真正烧进芯片的文件）
startup.o ┘
```

**关键点：每个 .c 是单独编译成 .o 的，编译时互相不知道对方的存在**，
最后才由**链接器**按"符号名"把它们拼起来。

`#include "encoder.h"` **不是**让 main.c 看到 encoder.c 的实现，
它只是把**头文件里的文字**粘进 main.c（头文件里通常只有"声明"，没有"实现"）。

---

## 2. static 函数在 main.c 里会发生什么

我在 `encoder.c` 里写了：

```c
static void Encoder_ApplyPwm(uint16_t duty)   /* 只在本文件存在的私有函数 */
{
    uint16_t ccr = ...;
    TIM_SetCompare1(TIM4, ccr);
}
```

如果我在 `main.c` 里写 `Encoder_ApplyPwm(500);`：

| 阶段 | 结果 |
|---|---|
| VS Code / Keil 的代码提示里 | ❌ 根本搜不到这个名字（编辑器只认 main.c 里能看见的声明） |
| 编译 main.c | ⚠️ 未声明标识符 / 隐式声明（C99 下是错误） |
| 手动补一句原型 `void Encoder_ApplyPwm(uint16_t);` 后编译 | ✅ main.o 编译通过 |
| **链接** | ❌ `Undefined symbol Encoder_ApplyPwm (referred from main.o)` |

**原因**：`static` 让这个函数在 `encoder.o` 的符号表里被记成 **Local（局部符号）**，
链接器**不会**用别的 .o 里的引用来匹配它。
所以"看不到"的真正含义不是文件权限，而是——**链接器层面根本不存在这个名字可供引用**。

---

## 3. 一张表：什么能被别的文件使用

| 在 `a.c` 里写 | `b.c` 能不能用 | 需要什么 |
|---|---|---|
| `static void f(void){}` | ❌ 不能 | 去掉 `static` + 在头文件里声明 |
| `void f(void){}`（非 static） | ✅ 能 | 头文件里有 `void f(void);`（或自己写 `extern void f(void);`） |
| `static int s_x;` | ❌ 不能按名字访问 | 提供非 static 的 getter/setter |
| `int g_x;` | ✅ 能 | `extern int g_x;` 声明 |
| 函数**定义**写在头文件里 | ✅ 能（每个 .c 各一份副本） | 不推荐，除非 `static inline` |

对照我的工程：
- `ic.h` 里的 `IC_Init()`、`IC_GetFreqHz()` 是"给外面用的窗户"；
- `ic.c` 里的 `s_period_us`、`pwm.c` 里的 `PWM_DeadTimeFromNs()` 是"自己家的抽屉"，外面不需要也不应该看见。

---

## 4. 最直观的反例：`main()` 和 `TIM3_IRQHandler()` **绝对不能** static

```c
/* stm32f10x_it.c */
void TIM3_IRQHandler(void)     /* ← 不能加 static！ */
{
    IC_TIM3_IRQHandler();
}
```

因为 `startup_stm32f10x_md.s`（汇编启动文件，**另一个翻译单元**）里有一张中断向量表：

```
DCD  TIM3_IRQHandler        ; 这一行就是"按名字"引用这个函数
DCD  SysTick_Handler
...
LDR  R0, =__main            ; 启动代码也是按名字找 main
```

如果 `TIM3_IRQHandler` 加了 `static`，链接器会报
`Undefined symbol TIM3_IRQHandler (referred from startup_stm32f10x_md.o)`，
中断永远进不来；同理 `main()` 也不能 static。

**规律：凡是要被"外部按名字点名调用"的（main、中断服务函数、给别人用的 API），
必须非 static；纯内部实现细节，鼓励 static。**

---

## 5. 三个可以亲手做的实验

**实验 1（证明 static 看不见）**
在 `03_Encoder\USER\main.c` 加一行 `Encoder_ApplyPwm(500);` → 链接报 `Undefined symbol`；
再把 `encoder.c` 里的 `static` 删掉、在 `encoder.h` 加 `void Encoder_ApplyPwm(uint16_t duty);` → 立刻通过。

**实验 2（证明 static 会避免重名冲突）**
在两个不同的 .c 里各写 `static uint32_t s_duty[4];` → 编译链接都通过（各一份独立副本）；
把两处 `static` 都删掉 → 立刻报 **multiply defined symbol**。
→ 这就是我给所有内部变量都加 static 的真正原因。

**实验 3（看编译产物，最有说服力）**
编译成功后打开 Keil 生成的 `Objects\你的工程.map`，搜索：

| 搜什么 | 出现在哪一类 | 说明 |
|---|---|---|
| `Encoder_ApplyPwm` | **Local Symbols**（后面跟着 `encoder.o`） | 私有，外部不可见 |
| `Encoder_GetRpm` | **Global Symbols** | 对外 API |
| `TIM3_IRQHandler` | **Global Symbols** | 被启动文件引用 |

看懂这条分界线，static 就再也不会模糊了。

---

## 6. 顺带一个坑：头文件里不要写 static（变量或函数）

```c
/* xxx.h —— 反面教材 */
static uint32_t s_cnt = 0;          /* ❌ 每个 #include 它的 .c 都生成一份副本 */
static void helper(void) { ... }    /* ❌ 每个 .c 一份代码，白占 Flash，还可能重名 */
```

因为 `#include` 是**文本粘贴**，头文件里的 static 会在每个翻译单元里各造一份。
头文件里只放：**函数声明、宏、类型定义、`extern` 声明**。
（唯一例外是 `static inline`，那是"故意每个文件一份、且允许不生成实体"的写法。）

---

## 7. 一句话总结

> `static` = **"这个符号只在我这一个 .c（翻译单元）里存在"**。
> 所以你能"看到"的，只有头文件里声明过的、非 static 的东西。
> 我写的那堆 static 就是设计成"你在 main.c 里看不见，也不用看见"——
> 你只需要调用 `IC_GetFreqHz()`、`Encoder_GetRpm()` 这些门口的函数。
