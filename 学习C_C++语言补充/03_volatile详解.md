# 03 · `volatile` 详解（含为什么必须和什么时候不必）

## 1. `volatile` 的唯一含义

> **编译器不许假设这个变量的值没被别人改过。每次使用它，都必须真的去内存读/写；
> 不许缓存到寄存器，也不许把"看起来没用"的读写优化掉。**

它管的是**访问方式**，不管可见性、不管原子性、不管同步。

## 2. 必须加 volatile 的场景

| 场景 | 例子 | 不加会怎样 |
|---|---|---|
| **中断里写、主循环读** | `static volatile uint32_t s_period_us;` | 主循环可能一直读到寄存器里的旧值 |
| 主循环写、中断里读 | `static volatile uint8_t s_enable;` | 中断里可能永远看不到新值 |
| 主循环设置"标志"给中断/其他上下文 | `s_sample = 1u;` | 编译器可能把这个赋值优化掉 |
| 轮询硬件寄存器 | `while ((USART1->SR & (1<<7)) == 0);` | 循环可能被优化成死循环（CMSIS 已把寄存器定义成 `__IO` = volatile） |
| 调试器要看变化的全局变量 | Keil Watch 里的 `g_xxx` | 变量可能只在寄存器里，Watch 窗口看不到变化 |
| RTOS 里多任务共享的裸变量 | 任务 A 写、任务 B 读 | 同"中断"情况 |

**我们工程里的真实例子：**

```c
/* ic.c —— 中断写，主循环通过 getter 读 */
static volatile uint32_t s_period_us = 0;
static volatile uint32_t s_high_us   = 0;
static volatile uint8_t  s_lost      = 1u;
static volatile uint32_t s_idle_ms   = 0;      /* SysTick 中断里 ++ */

/* encoder.c —— 典型的生产者/消费者标志 */
static volatile uint8_t  s_sample = 0;
void Encoder_SysTickHandler(void) { ... s_sample = 1u; }        /* 中断: 置位 */
void Encoder_Update(void) { if (s_sample == 0u) return; s_sample = 0u; ... } /* 主循环: 清零 */
```

**如果 `s_sample` 不加 volatile**：优化器"看不出"谁会改它，可能把
`if (s_sample == 0u) return;` 的读取提到循环外，于是你转编码器也永远等不到更新
—— 这就是新手最典型的"逻辑没错但就是不工作"。

## 3. volatile **不解决**什么（别当同步原语）

| 它不管 | 说明 | 正确做法 |
|---|---|---|
| ❌ 原子性 | `s_position += delta` 是"读→改→写"三步，中断里也这么写就可能丢更新 | 关中断 `__disable_irq()/__enable_irq()`，或用原子操作 |
| ❌ 内存屏障/顺序 | CPU/编译器仍可能调整无关指令顺序 | 用 `__DMB()`/`__DSB()` 或语言级原子（C11 atomics、C++ std::atomic） |
| ❌ 互斥 | 不阻止两个上下文同时写 | 关中断 / 临界区 / RTOS 互斥量 |
| ❌ 访问控制 | 与 static/private 无关 | 数据 `static` + 提供接口 |

**Cortex-M3 的原子性规则（重要）**：
- **对齐的 8/16/32 位变量的读或写是单条指令 → 天然原子**，所以我在主循环里直接读
  `s_period_us`、`TIM3->CNT` 是安全的；
- 64 位、结构体、数组这类多字节数据 → **必须**临街区（关中断）或双缓冲，加 volatile 也没用。

## 4. 什么时候**不需要** volatile

```c
static uint32_t s_duty[4];      /* 只在主流程读写, 中断不碰 → 不需要 */
static uint16_t s_last_cnt;     /* 同上 */
int    local_var;               /* 函数局部变量 → 更不需要 */
```
加了反而有害：编译器无法把它放进寄存器，每次访问都多一次内存读写（速度变慢、代码变大）。

## 5. `static` 与 `volatile` 的关系（经常一起出现）

```c
static volatile uint32_t s_period_us;
```
| 修饰 | 作用 | 去掉它 |
|---|---|---|
| `static` | 名字只在本 .c 可见 | 变成全局符号，可能和其他文件重名 |
| `volatile` | 每次真读真写内存 | 可能读到过期值 / 优化掉读写 |

**两个解决完全不同的问题，互不替代。** 一句记忆：
> **static 管"谁能看见我"，volatile 管"必须真的去内存看我"。**

## 6. 常见错误

| 错误 | 后果 |
|---|---|
| 中断共享变量忘了 volatile | 偶发读不到新值，加个 printf / 加点代码又"好了"（最坑人的一类 bug） |
| 用 volatile 当"线程安全" | 多字节数据仍可能撕裂；竞态依旧存在 |
| 把所有全局变量都加 volatile | 性能下降、代码变大，而且掩盖了真正该保护的临界区 |
| 用 volatile 保护 `static` 数组 | 数组元素多字节操作仍非原子 |
| 以为 `volatile` 能让 Watch 窗口看到值 | 它是"防止优化"，调试观察是副作用；真正目的是正确性 |

## 7. 写代码时的自查清单

1. 这个变量会不会被**中断/其他任务**修改？→ 会：加 `volatile`
2. 它需要被**别的 .c** 按名字访问吗？→ 不需要：加 `static`
3. 它是不是**多字节**且被并发访问？→ 是：关中断/双缓冲，不能只靠 volatile
4. 我是想保护它**不被乱改**吗？→ 那不是 volatile 的活：数据 `static` + 只暴露接口
5. 它只被本函数使用吗？→ 是：普通局部变量就好（**别**加 volatile）
