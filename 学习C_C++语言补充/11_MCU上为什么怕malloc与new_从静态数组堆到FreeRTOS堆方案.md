# 11 · MCU 上为什么怕 `malloc` / `new` —— 从"静态数组堆"到 FreeRTOS `heap_1~5`

> **记录时间**：2026-09-20
> **触发场景**：写完 `09_内存分配到底怎么回事_栈堆与new.md` 后自然要问：**STM32 上能不能也这么用 `new` / `malloc`？**
> **分类判定**：拿掉 ROS2 成立、换到 PC 也成立（是语言 / 运行时 / 分配器问题）→ **C/C++ 语言补充** ✅
> （**具体到某个工程的堆配置**，将来写进 `0X_工程名\README.md`；本篇只讲原理与纪律）
> **关联**：本篇是 `09` 的 **"MCU 版续集"**（`09` 讲 PC 分配器，本篇讲 MCU 分配器）

---

## 1. 一句话结论

1. **PC 的堆能"长大"**（分配器可以 `brk`/`mmap` 再向操作系统要），
   **MCU 的堆是一块固定大小的静态数组**（`ucHeap[]` + `configTOTAL_HEAP_SIZE`）—— **要不到了**。
2. 所以**碎片化在 MCU 上直接等于"分配失败"**：**总空闲够、最大连续块不够 → 失败**
   （实测三组情形，**连会合并的 `heap_4` 也救不了"夹心"碎片**）。
3. `malloc`/`new` 在实时系统里还有**不确定的执行时间**（找空闲块、合并的耗时随堆状态变化）
   → **控制环 / 中断里绝不能出现**。
4. 固件里的正解：**静态分配**（全局/静态数组）、**对象池**、`xTaskCreateStatic` / `xQueueCreateStatic`、
   **环形缓冲（静态数组 + 读写指针）**；真要用堆，**只在启动阶段一次性分配完，之后不再分配**。

---

## 2. 现象（实测原文，原样保留）

程序：`/tmp/w1_mcu_heap_demo.c` —— **用 256 字节静态数组当堆**（不是真 FreeRTOS，是把原理抽出来跑）
`sizeof(Block)=24`；`coalesce=0` 模拟 `heap_2`（释放后**不**回并），`coalesce=1` 模拟 `heap_4`（**会**回并）。

```
=== 情形一：释放相邻的三块 p0/p1/p2（coalesce=0，≈heap_2 不合并）===
      [free 16] [free 16] [free 16] [used 16] [used 16] [free 32]
   总空闲 = 80 字节，最大连续空闲 = 32 字节
   现在申请 40 字节 → 失败 ❌（总量够，但没有连续的一块）

=== 情形一：释放相邻的三块 p0/p1/p2（coalesce=1，≈heap_4 会合并）===
      [free 96] [used 16] [used 16] [free 32]
   总空闲 = 128 字节，最大连续空闲 = 96 字节
   现在申请 40 字节 → 成功 ✅

=== 情形二：释放分散的三块 p0/p2/p4（coalesce=0）===
      [free 16] [used 16] [free 16] [used 16] [free 16] [free 32]
   总空闲 = 80 字节，最大连续空闲 = 32 字节
   现在申请 40 字节 → 失败 ❌

=== 情形二：释放分散的三块 p0/p2/p4（coalesce=1）===
      [free 16] [used 16] [free 16] [used 16] [free 72]     ← p4 与后面的尾巴并成了 72
   总空闲 = 104 字节，最大连续空闲 = 72 字节
   现在申请 40 字节 → 成功 ✅

=== 情形三：释放夹心的两块 p1/p3（coalesce=0）===
      [used 16] [free 16] [used 16] [free 16] [used 16] [free 32]
   总空闲 = 64 字节，最大连续空闲 = 32 字节
   现在申请 40 字节 → 失败 ❌

=== 情形三：释放夹心的两块 p1/p3（coalesce=1）===   ← ★ 重点
      [used 16] [free 16] [used 16] [free 16] [used 16] [free 32]
   总空闲 = 64 字节，最大连续空闲 = 32 字节
   现在申请 40 字节 → 失败 ❌   ← 合并也救不了：两边都被 used 夹住
```

**六组结果一张表**：

| 情形 | 不合并（heap_2 风格） | 会合并（heap_4 风格） |
|---|---|---|
| ① 释放相邻三块 | 总 80 / 最大 32 → **失败** | 总 128 / 最大 96 → **成功** |
| ② 释放分散三块 | 总 80 / 最大 32 → **失败** | 总 104 / 最大 72 → **成功**（末块与尾巴并了） |
| ③ 释放夹心两块 | 总 64 / 最大 32 → **失败** | 总 64 / 最大 32 → **失败** ❌ |

> 📌 **结论**：**"总空闲够"完全没有意义，只有"最大连续块够"才算数。**
> 而在 MCU 上堆**不能长大** → 这种失败**没法靠"再要一点内存"绕过** → 现场就是**功能挂掉 / 任务创建失败**。

---

## 3. 为什么会这样（原理）

### 3.1 PC 的堆 vs MCU 的堆

| | PC（Linux） | MCU（STM32 + FreeRTOS） |
|---|---|---|
| 堆从哪来 | `brk`/`mmap` **向操作系统要**，可以一直变大（虚拟内存） | **一块静态数组**（`ucHeap[]`，大小由 `configTOTAL_HEAP_SIZE` 定死） |
| 分配失败会怎样 | 几乎见不到（不行就换页/换地址） | **真的返回 `NULL`** → 你的代码必须处理 |
| 谁在分配 | glibc `malloc`（chunk + 元数据 + 合并） | `pvPortMalloc`（`heap_1~5` 之一） |
| 碎片化后果 | 难受但能扛 | **直接失败** |
| 时间可预测性 | 不在乎 | **极在乎**（控制环周期毫秒级） |

### 3.2 FreeRTOS 的 5 种堆（`portable/MemMang/heap_*.c`）—— **差异就在"能不能 free / 会不会合并 / 能不能用多块 RAM"**

| 方案 | 能分配 | 能释放 | 合并相邻空闲块 | 允许不连续的内存区 | 适用场景 |
|---|:---:|:---:|:---:|:---:|---|
| **heap_1** | ✅ | ❌ | — | ❌ | **最简单**：启动时建完任务就再也不分配（确定性最好、无碎片） |
| **heap_2** | ✅ | ✅ | ❌ | ❌ | 已不推荐（会产生碎片）；旧教程里常见 |
| **heap_3** | ✅ | ✅ | 交给 `malloc` | — | 用你自己的 `malloc`（配 `configTOTAL_HEAP_SIZE` 无效，看链接脚本） |
| **heap_4** | ✅ | ✅ | ✅ | ❌ | **最常用**（首次适配 + 相邻回并） |
| **heap_5** | ✅ | ✅ | ✅ | ✅ | 需要**多块不连续 RAM**（内部 SRAM + 外部 SDRAM）时，用 `vPortDefineHeapRegions()` 声明 |

常用 API 与配置（等你 W7 上 FreeRTOS 时会用到）：

| 项 | 说明 |
|---|---|
| `pvPortMalloc()` / `vPortFree()` | 就是"FreeRTOS 版 malloc/free"；**任务里建议尽量别用** |
| `xPortGetFreeHeapSize()` | 当前剩余堆；`xPortGetMinimumEverFreeHeapSize()` **历史最小**（最能暴露"快崩了"） |
| `configTOTAL_HEAP_SIZE` | `heap_1/2/4/5` 的堆大小（**不是设得越大越好**：那是从 RAM 里挖走的） |
| `configSUPPORT_DYNAMIC_ALLOCATION` / `configSUPPORT_STATIC_ALLOCATION` | 是否允许**动态**创建 / 是否启用**静态**创建 API |
| `xTaskCreateStatic()` / `xQueueCreateStatic()` / `xEventGroupCreateStatic()` | **静态创建**：内存你自己给（编译期就确定，最稳） |
| `configAPPLICATION_ALLOCATED_HEAP` | 允许你自己定义 `ucHeap[]`（可以放到外部 RAM / 指定段） |

> ⚠️ 注：我在你 **D 盘上找过 FreeRTOS 源码** —— `Keil\ARM\PACK\ARM\` 里**目前只有 `CMSIS`**（没有 `CMSIS-FreeRTOS`），
> 你的 `STM32F103C8_MotorSpeed` 里也没有 FreeRTOS 目录。
> 所以上面的表格是**通用规则**；等你 W7 真正装 FreeRTOS 时，请**对着自己那份 `heap_*.c` 核对一遍**（第 1 行注释里就写了各方案的适用场景）。

### 3.3 为什么"时间不确定"比"失败"更可怕

`malloc` 的耗时**取决于当前堆的状态**（要找多长的链、要不要合并）。
控制环里如果需要"每 1 ms 保证执行完"，**这种不确定就是致命的** ——
它不会每次都超时，但**偶尔**超时一次，你的 PID 就抖一下、机器人就晃一下（这种 bug 极难复现）。
**静态分配 = 零搜索、零合并，时间恒定。**

### 3.4 那"环形缓冲"为什么是固件最爱？（连接你的 W2 计划）

`00_8个月学习计划.md` §14 的 **W2 = F103 UART（中断 + 环形缓冲）**，
环形缓冲 = **一块静态数组 + 两个下标（读写指针）**：

```
    static uint8_t buf[256];  uint16_t rd = 0, wr = 0;
    wr = (wr + 1) & 255;   // 中断里写，只做取数+置标志（符合代码风格第 4 条）
```

- **零 `malloc`、零碎片、耗时恒定**（就是两次自增 + 一次掩码）；
- 满了就**覆盖/丢弃**（策略明确），而不是"分配失败"；
- 这也是为什么 STM32 的驱动几乎都这么写。

---

## 4. 最小复现（不依赖任何硬件，PC 上就能看到"MCU 的痛"）

```c
/* w1_mcu_heap_demo.c —— 用【静态数组】当堆，看碎片化
 *   coalesce=0 ≈ heap_2（不合并）   coalesce=1 ≈ heap_4（合并）
 */
#include <stdio.h>
#include <stddef.h>

#define HEAP_SIZE 256u                    /* ← 相当于 configTOTAL_HEAP_SIZE（固定，长不大） */
#define ALIGN8(n) (((n) + 7u) & ~7u)

typedef struct Block { size_t size; int free; struct Block *next; } Block;   /* sizeof=24 */

static unsigned char g_heap[HEAP_SIZE];   /* ← 相当于 FreeRTOS 的 ucHeap[] */
static Block *       g_head;

static void heap_init(void)
{ g_head = (Block *)g_heap; g_head->size = HEAP_SIZE - sizeof(Block); g_head->free = 1; g_head->next = NULL; }

static void *my_malloc(size_t n)
{
    n = ALIGN8(n);
    for (Block *b = g_head; b; b = b->next) {
        if (b->free && b->size >= n) {
            if (b->size >= n + sizeof(Block) + 8u) {          /* 够切就切一刀 */
                Block *rest = (Block *)((unsigned char *)(b + 1) + n);
                rest->size = b->size - n - sizeof(Block); rest->free = 1; rest->next = b->next;
                b->size = n; b->next = rest;
            }
            b->free = 0; return (void *)(b + 1);
        }
    }
    return NULL;                          /* ← MCU 上就是“分配失败”：没有 brk/mmap 可以再要 */
}

static void my_free(void *p, int coalesce)
{
    if (!p) { return; }
    Block *b = (Block *)p - 1; b->free = 1;
    if (coalesce) {                        /* heap_4 的相邻回并 */
        for (Block *cur = g_head; cur; cur = cur->next)
            while (cur->next && cur->free && cur->next->free) {
                cur->size += sizeof(Block) + cur->next->size; cur->next = cur->next->next;
            }
    }
}

static int total_free(void)   { int s = 0; for (Block *b = g_head; b; b = b->next) if (b->free) s += (int)b->size; return s; }
static int largest_free(void) { int m = 0; for (Block *b = g_head; b; b = b->next) if (b->free && (int)b->size > m) m = (int)b->size; return m; }

int main(void)
{
    for (int coalesce = 0; coalesce <= 1; ++coalesce) {
        char *p[5];
        heap_init();
        for (int i = 0; i < 5; ++i) { p[i] = (char *)my_malloc(16); }
        my_free(p[1], coalesce); my_free(p[3], coalesce);      /* 夹心：两边都被 used 挡住 */
        printf("coalesce=%d：总空闲=%d，最大连续=%d，申请 40 → %s\n",
               coalesce, total_free(), largest_free(),
               my_malloc(40) ? "成功 ✅" : "失败 ❌");
    }
    return 0;
}
```

```bash
gcc -std=c11 -Wall -Wextra -o /tmp/w1_mcu_heap_demo /tmp/w1_mcu_heap_demo.c && /tmp/w1_mcu_heap_demo
```

完整版（含"相邻/分散/夹心"三种情形的 6 组对照）见 `/tmp/w1_mcu_heap_demo.c`，输出已抄在本文 **§2**。

---

## 5. 要不要 / 什么时候用（固件的判断标准）

| 场景 | 能不能 `malloc`/`new` | 怎么做 |
|---|---|---|
| **启动阶段一次性分配**（建缓冲、建对象） | 🟡 理论上可以，但**优先静态** | `xTaskCreateStatic` / 全局静态数组；静态化后**永不释放** |
| **运行期周期性分配/释放** | ❌ **不建议** | 对象池 / 环形缓冲 / 预分配队列 |
| **中断服务函数里** | ❌ **绝对不要** | 中断只"**取数 + 置标志**"（你自己定的代码风格第 4 条） |
| 大小固定的东西（任务栈、队列、协议帧） | ✅ 静态 | 编译期就确定大小，`sizeof` 能算出来 |
| 变长缓冲（UART 接收、日志打印） | ✅ 用环形缓冲 | 静态数组 + 读写指针 + **丢最旧/丢最新**策略 |
| STM32 裸机（无 RTOS） | 一般**根本不用堆** | Keil 工程的启动文件里 `Heap_Size` 通常只给几百字节 → 用了迟早炸 |
| 用了 FreeRTOS | 看是哪个 `heap_x` | 只用 heap_1（不释放）或 heap_4（要释放）；并用 `xPortGetMinimumEverFreeHeapSize()` 盯住 |

> **一句话**：**能在编译期算出来的内存，绝不留到运行期去要。**

---

## 6. 正确 ✗ 错误 对照（MCU 版）

| ✗ 错误 | ✅ 正确 | 为什么 |
|---|---|---|
| 任务循环里 `malloc`/`free` | 静态池 / 环形缓冲 | 碎片 + 时间不确定 |
| 中断里 `malloc` | 中断只置标志，主循环处理 | 中断里不能有不确定耗时/不可重入风险 |
| `malloc` 之后不判 `NULL` | **每次都判** | PC 上养成的不判习惯，到 MCU 直接崩 |
| 用 `xPortGetFreeHeapSize()` 看"还剩多少"就放心 | 还要看 `xPortGetMinimumEverFreeHeapSize()` | 平均值好看，**历史最低**才暴露隐患 |
| 把 `configTOTAL_HEAP_SIZE` 调到最大当解法 | 先问"最大连续块够不够"；优先静态 | RAM 是有限资源，挖走堆就是挖走别的 |
| 用 `heap_2` 跑频繁 alloc/free | 换 `heap_4`（会合并）或**不用堆** | 不合并 = 碎片制造机 |

---

## 7. 口诀 / 排查清单

> **口诀：PC 的堆会长大，MCU 的堆是块死数组；总空闲不算数，最大连续才算数。**

| # | 排查项 | 怎么看 |
|:---:|---|---|
| 1 | 堆多大？ | `configTOTAL_HEAP_SIZE`（heap_1/2/4/5）或链接脚本（heap_3） |
| 2 | 用的是哪个 `heap_x.c`？ | 工程里只应存在**一个** |
| 3 | 谁在分配？ | 搜 `pvPortMalloc` / `malloc` / `new`；看是不是在启动阶段 |
| 4 | 运行期还在分配吗？ | 若"是" → 想办法改成静态/池 |
| 5 | 余量够吗？ | `xPortGetMinimumEverFreeHeapSize()`（**历史最低**） |
| 6 | 中断里有没有分配？ | 搜 ISR（`*_IRQHandler`）里的 `malloc`/`pvPortMalloc` |
| 7 | 裸机工程的 `Heap_Size` | 启动文件 `startup_stm32f10x_md.s` 里的 `Heap_Size EQU 0x200` 之类 |

---

## 8. 关联知识

| 项 | 内容 |
|---|---|
| 复现代码 | §4（`/tmp/w1_mcu_heap_demo.c`，重启会清，故已内嵌） |
| 相关笔记 | **`09_内存分配到底怎么回事_栈堆与new.md`（PC 分配器）**、`10_对象包含关系与方法里new出来的内存归谁.md` |
| FreeRTOS 源码位置 | `portable/MemMang/heap_1.c … heap_5.c`（官方：`github.com/FreeRTOS/FreeRTOS-Kernel/tree/main/portable/MemMang`）<br>⚠️ **本机现状**：`D:\Keil\ARM\PACK\ARM\` 里目前**只有 CMSIS**（未装 `CMSIS-FreeRTOS`），你的 `STM32F103C8_MotorSpeed` 工程里也没有 FreeRTOS → **W7 装好后请对着自己那份 `heap_*.c` 核对本篇表格** |
| 计划接口 | `00_8个月学习计划.md` §14 **W2（F103 UART 中断 + 环形缓冲）**、**W7 起（FreeRTOS）**；`..\成长路线\README.md` 代码风格第 4 条 |
| 将来要写进工程 README 的 | 具体工程的"堆大小 / 用了哪个 heap_x / 哪些地方是静态分配" → 写进 `0X_工程名\README.md` |

---

## 9. 一句话复述（自测用，能背下来才算过）

> MCU 的堆是**一块固定大小的静态数组**，**长不大** —— 所以碎片化在那里**直接等于功能失败**，
> 而且"总空闲够"毫无意义，**只有"最大连续块够"才算数**；
> 因此固件里**能在编译期算出来的内存绝不留给运行期**：静态数组、对象池、环形缓冲、
> `xTaskCreateStatic` / `xQueueCreateStatic`；用 FreeRTOS 的堆也只当"启动期一次性分配"的工具。

---

*笔记结束 ｜ 2026-09-20*

