# 09 · 内存分配到底怎么回事 —— `new` / `make_shared` / 栈与堆

> **记录时间**：2026-09-20
> **触发场景**：看完 `08_对象内存布局与析构时机.md` 后仍不满足，想把"内存分配"这层彻底搞清
> **原始疑问（自己 README 原文摘录）**：
> > "……为什么不直接把临时指针变成堆区的指针？……
> > 如果做成堆区的指针，那么就会造成内存浪费，只需要一个智能指针指向 publish 这一类功能的类就行了……
> > 如果有两个智能指针，那么做一个 node 对象被析构的假设，`pub_` 虽然被释放了，
> > 但是内部的临时智能指针被我假定是堆区后的智能指针没有被我销毁，我的 publish 依然存在……
> > 我暴露了一个很大的问题，就是**对于内存生成这个概念我不是很熟悉**，
> > 这直接决定了我前面说的是不是对的……"
> **对应源码**：`~/ros2_study_myself/w1_study_workspace/src/w1_topic_demo/src/topic_publisher_demo.cpp:11,20`

---

## 1. 一句话结论

**"内存在哪"和"谁拥有它、活多久"是两套互不决定的机制**，必须分开谈：

| 机制 | 管什么 | 由谁决定 |
|---|---|---|
| **① 内存位置**（栈 / 堆 / 静态区） | 这块**存储**什么时候被回收 | 编译器（栈）/ 分配器（堆）/ 程序生命周期（静态） |
| **② 所有权 / 生命周期**（引用计数） | 被指的**对象**什么时候被析构 | `shared_ptr` 的计数：**最后一个持有者** |
| **③ 分配器层**（`new` 实际做了什么） | 地址长什么样、连不连续 | 分配器（glibc malloc）：元数据 + 对齐 |

三条推论（全部有实测支撑）：

1. **`new` 只保证"一块内部"连续**：块与块之间有**分配器元数据 + 对齐**（实测：相邻 `new` 的地址差 **80** 字节，而对象只有 **64** 字节）。
2. **`make_shared<T>()` 分配 1 次（实测 80 字节）；`shared_ptr<T>(new T)` 分配 2 次（实测 64 + 24）**。
3. **"把智能指针放到堆上"≠"让对象一直安全地活着"**：没人 `delete` 就是**内存泄漏**（实测：构造 +1 / 析构 **+0**）；有人 `delete` 就还是会死。
   → 真正该做的是：**交给一个生命周期正确的所有者持有**（成员 / 容器 / 静态）。

---

## 2. 实测（原样保留，便于以后搜索）

程序：`/tmp/w1_alloc_demo.cpp`（重载了全局 `operator new` 来数分配；用 `/proc/self/maps` 判断地址落在哪个段）。

### 2.1 同一类型对象放在三个地方 → 落在三个不同的段

```
[Payload 构造] this=0x64a46a11d060
[Payload 构造] this=0x7fff9fccdff0
[operator new] 第 1 次，请求 64 字节
[Payload 构造] this=0x64a483b812c0
static_obj(静态) 0x64a46a11d060 → [rw-p] ... /tmp/w1_alloc_demo      ← 可执行文件的数据段
stack_obj (栈)   0x7fff9fccdff0 → [rw-p] ... [stack]
heap_obj  (堆)   0x64a483b812c0 → [rw-p] ... [heap]
--- maps 里的段 ---
64a483b6f000-64a483b90000 rw-p ... [heap]
7fff9fcaf000-7fff9fcd1000 rw-p ... [stack]
```

### 2.2 一次 `new` 只保证"这一块"内部连续

```
v[0] = 0x64a483b814f0
v[1] = 0x64a483b81540
v[2] = 0x64a483b81590
相邻地址差 = 80 字节，而 sizeof(Payload) = 64
malloc_usable_size(v[0]) = 72（> 请求的 64 → 分配器多给了）
```

### 2.3 `make_shared` 1 次 vs `shared_ptr(new)` 2 次

```
[operator new] 第 15 次，请求 80 字节      ← make_shared：控制块+对象 一次搞定
make_shared<Payload>()           → 分配 1 次
[operator new] 第 16 次，请求 64 字节      ← 先给对象
[operator new] 第 17 次，请求 24 字节      ← 再给控制块
shared_ptr<Payload>(new Payload) → 分配 2 次
sizeof(shared_ptr<Payload>) = 16（= 对象指针 8 + 控制块指针 8）
```

### 2.4 多个 `shared_ptr` 副本 ≠ 多个对象

```
auto sp3 = sp1; → 额外分配 0 次（0 = 没新建对象）
sp1.get()=0x64a483b815f0  sp3.get()=0x64a483b815f0  use_count=2
```

### 2.5 "把智能指针放到堆上"的两种结局

```
4a) 放堆上【并且】手动 delete：
      [operator new] 第 18 次，请求 16 字节     ← 那个放在堆上的 shared_ptr 本体（16 字节）
      heap_sp->use_count() = 1
      [Payload 析构] this=0x64a483b816e0
      构造 +1 / 析构 +1 → 正常销毁，没漏

4b) 放堆上【但没人 delete】：
      heap_sp->use_count() = 1
      构造 +1 / 析构 +0 → 出作用域后仍然 0 个析构！
      （没有出现 [Payload 析构] —— 这就是泄漏）
```

### 2.6 程序退出时的析构顺序（局部智能指针先于栈/静态对象）

```
sp1.use_count=2  sp2.use_count=1  sp3.use_count=2
[Payload 析构] 0x64a483b81640     ← sp2 独占的那个（后声明先析构）
[Payload 析构] 0x64a483b815f0     ← sp1/sp3 共享的那个
[Payload 析构] 0x7fff9fccdff0     ← stack_obj（栈）
[Payload 析构] 0x64a46a11d060     ← static_obj（静态）
```

---

## 3. 为什么会这样（原理）

### 3.1 进程的地址空间有"段"，不同段有不同的回收规则

| 段 | 里面放什么 | 什么时候回收 | 实测地址样子 |
|---|---|---|---|
| **栈 [stack]** | 局部变量、函数参数（`Payload stack_obj;`） | 出作用域自动回收 | `0x7fff…`（很高） |
| **堆 [heap]** | `new` / `malloc` 出来的东西 | **不会自动回收**，必须 `delete`/`free`（或由智能指针计数归零时代劳） | `0x64a4…`（很低） |
| **静态区**（可执行文件的数据段） | 全局变量、`static` 变量、字符串字面量 | 程序结束时统一回收 | 落在可执行文件自己的映射里 |
| 代码段 | 机器指令 | 只读，不涉及 | `r-xp` |

> ⚠️ 关键点：**"栈 vs 堆"决定的是"这块存储何时被回收"，与"谁拥有对象"是两件独立的事**。
> 这也解释了你的直觉为什么会打架：你把"放到堆上"当成了"让它一直存在"的手段，
> 但堆的特点是"**不会自动回收**"——**不代表"不会被回收"**：要么你/别人 `delete`（它就死），
> 要么谁也不 `delete`（**它就泄漏**）。**两条路都不是"安全地一直活着"。**

### 3.2 `new` 到底做了什么？（分配器层）

```
你的代码            new Payload
   ↓
全局 operator new   向分配器要 sizeof(Payload) = 64 字节
   ↓
glibc malloc        返回一块【chunk】：[元数据头][你请求的 64 字节][对齐填充]
   ↓
返回的地址          指向"你请求的那 64 字节"的开头（不是 chunk 的开头）
```

实测印证：
- 相邻两次 `new`（同一大小）地址差 **80** = 64（对象）+ 16（chunk 头）→ **块与块之间不连续**；
- `malloc_usable_size` 返回 **72** > 请求的 64 → 分配器按自己的规则给（对齐/取整）。

> 所以"**new 的内存是连续的**"这句话：
> ✅ **对象内部**的成员是连续排布的（`08` 篇实测偏移 8/24）；
> ❌ **多次 new 之间**不连续 —— 中间夹着分配器元数据与对齐填充。

### 3.3 `make_shared` 为什么更省？（两种写法的内存图）

```
std::make_shared<Payload>()                    ← 1 次分配（实测 80 字节）
┌───────────────────────────────────┐
│ 控制块(计数/deleter) │ Payload 对象 │   同一块内存，指针跳一次就到
└───────────────────────────────────┘
   ↑                    ↑
 control block        返回给你的 shared_ptr.get()

std::shared_ptr<Payload>(new Payload())        ← 2 次分配（实测 64 + 24）
┌──────────────────────┐        ┌──────────────────────┐
│ Payload 对象 (64)     │        │ 控制块 (24)           │   两块内存，互不相邻
└──────────────────────┘        └──────────────────────┘
```

| 写法 | 分配次数 | 异常安全 | 局部性 |
|---|:---:|---|---|
| `make_shared<T>()` | **1** | 好（要么全成功） | 好（对象与控制块同页） |
| `shared_ptr<T>(new T)` | **2** | 弱（先 new 后建控制块，中间抛异常会漏） | 差（两次跳转） |

### 3.4 `shared_ptr` 这个"智能指针本体"住在哪？

- 它**自己也是一个对象**，实测 `sizeof(shared_ptr<Payload>) = 16`（对象指针 + 控制块指针）；
- 它住在哪，取决于**你把它声明在哪**：
  - 声明成**局部变量** → 在**栈**上（作用域结束 → 析构 → 计数 -1）；
  - 声明成**类的成员** → 内嵌在**那个对象**里（`08` 篇实测偏移 8）；
  - `new std::shared_ptr<T>(...)` → 在**堆**上（**必须有人 delete，否则连这 16 字节也漏**——实测 4b 里那次"请求 16 字节"）；
  - 声明成 `static` → 静态区（活到程序结束）。
- **多个 `shared_ptr` 副本共享同一个对象**：实测 `auto sp3 = sp1;` **额外分配 0 次**、`sp1.get() == sp3.get()`、`use_count=2`。
  → 所以"**两个智能指针**"并不等于"两个 Publisher 对象"，**不会因此浪费对象内存**，只是多了一份 16 字节的指针对。

### 3.5 五个位置 × 谁负责回收（背这张表）

| 对象放在哪 | 例子 | 谁负责回收 | 会不会泄漏 |
|---|---|---|---|
| 栈 | `Payload stack_obj;` | 编译器（出作用域） | ❌ 不会 |
| **某对象的成员** | `pub_`（在 `Node` 里） | 宿主对象析构时 | ❌ 不会 |
| 堆（裸 `new`，有人 delete） | `auto * p = new Payload; delete p;` | **你** | ⚠️ 忘了就漏 |
| 堆（裸 `new`，没人 delete） | `new std::shared_ptr<T>(...)` 后不管 | **没人** | ✅ **一定漏**（实测 4b） |
| 堆（由 `shared_ptr` 管） | `auto sp = make_shared<Payload>();` | 引用计数归零时自动 `delete` | ❌ 不会 |
| 静态区 | `static Payload x;` | 程序结束 | ❌ 不会 |

### 3.6 于是，"为什么不把它放到堆上"这个问题本身就消解了

| 你的期待 | 实际结果 |
|---|---|
| 放堆上 → 对象"一直都在" | ✅ 对象确实不死 —— **但代价是内存泄漏**（没人 delete） |
| 放堆上 → 更"安全" | ❌ 更不安全：栈上至少会**确定地**回收，堆上漏了就**确定地**漏 |
| "只要一个智能指针指向它就行了" | ✅ 对：**Publisher 本体只需要一个**；但要它一直活着，需要**一个生命周期正确的所有者**（成员/容器/静态），而不是"把它搬到堆上" |

---

## 4. 最小复现（不依赖 ROS；重载 `operator new` 数分配 + 读 `/proc/self/maps` 判断段）

```cpp
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <new>
#include <string>
#include <malloc.h>

static long g_new_calls = 0;
static int  g_ctor = 0, g_dtor = 0;

void * operator new(std::size_t n)          // ← 关键：数“谁在要内存、要多少”
{
    ++g_new_calls;
    std::printf("      [operator new] 第 %ld 次，请求 %zu 字节\n", g_new_calls, n);
    void * p = std::malloc(n);
    if (p == nullptr) { throw std::bad_alloc(); }
    return p;
}
void operator delete(void * p) noexcept { std::free(p); }
void operator delete(void * p, std::size_t) noexcept { std::free(p); }

struct Payload {
    long pad[8];                            // 64 字节，方便看地址间隔
    Payload()  { ++g_ctor; std::printf("      [Payload 构造] this=%p\n", (void*)this); }
    ~Payload() { ++g_dtor; std::printf("      [Payload 析构] this=%p\n", (void*)this); }
};

static void where(const char * tag, const void * addr)   // 地址落在哪个段
{
    std::ifstream f("/proc/self/maps");
    std::string line;
    unsigned long a = (unsigned long)addr;
    while (std::getline(f, line)) {
        unsigned long lo = 0, hi = 0; char perms[8] = {0}, rest[512] = {0};
        if (std::sscanf(line.c_str(), "%lx-%lx %7s %511[^\n]", &lo, &hi, perms, rest) >= 3
            && a >= lo && a < hi) {
            std::printf("      %-16s %p → [%s] %s\n", tag, addr, perms, rest);
            return;
        }
    }
}

int main()
{
    static Payload static_obj;               // 静态区
    Payload stack_obj;                       // 栈
    Payload * heap_obj = new Payload;        // 堆
    where("static_obj(静态)", &static_obj);
    where("stack_obj (栈) ", &stack_obj);
    where("heap_obj  (堆) ", heap_obj);

    Payload * v[3];                          // 连续三次 new，看地址差
    for (int i = 0; i < 3; ++i) { v[i] = new Payload; }
    for (int i = 0; i < 3; ++i) { std::printf("      v[%d] = %p\n", i, (void*)v[i]); }
    std::printf("      相邻差 = %ld，sizeof(Payload) = %zu，usable = %zu\n",
                (long)((char*)v[1] - (char*)v[0]), sizeof(Payload), malloc_usable_size(v[0]));

    long c0 = g_new_calls;                   // 分配次数对照
    auto sp1 = std::make_shared<Payload>();
    std::printf("      make_shared → 分配 %ld 次\n", g_new_calls - c0);
    c0 = g_new_calls;
    std::shared_ptr<Payload> sp2(new Payload());
    std::printf("      shared_ptr(new) → 分配 %ld 次\n", g_new_calls - c0);

    c0 = g_new_calls;                        // 副本是否新建对象
    auto sp3 = sp1;
    std::printf("      auto sp3 = sp1 → 额外分配 %ld 次，use_count=%ld，get 相同=%d\n",
                g_new_calls - c0, sp1.use_count(), sp1.get() == sp3.get());

    int cc = g_ctor, cd = g_dtor;             // 4b：放堆上但没人 delete
    {
        auto * leaked = new std::shared_ptr<Payload>(std::make_shared<Payload>());
        (void)leaked;                        // 故意不 delete
    }
    std::printf("      放堆上不 delete：构造 +%d / 析构 +%d → 泄漏 %d 个\n",
                g_ctor - cc, g_dtor - cd, (g_ctor - cc) - (g_dtor - cd));
    return 0;
}
```

```bash
g++ -std=c++17 -Wall -Wextra -o /tmp/w1_alloc_demo /tmp/w1_alloc_demo.cpp && /tmp/w1_alloc_demo
```

实测输出见本文 **§2**（段归属、地址差 80、分配 1 次 vs 2 次、泄漏 1 个都印在那里）。

---

## 5. 要不要 / 什么时候用（判断标准）

| 情况 | 结论 |
|---|---|
| 需要"一个对象活到某个明确时刻" | 用**有着明确生命周期的所有者**（成员 / 容器 / `static`），**不要**靠"搬到堆上" |
| 只是想"不拷贝大对象" | 用引用 / `const&`（见 `06` 篇），**别动堆** |
| 需要"可以为空 / 可以换目标" | 指针或 `shared_ptr`（`shared_ptr` 可为空：`if (sp)`） |
| 需要**共享所有权**（谁都可能先走） | `shared_ptr`；**性能敏感**且明确单一所有者时用 `unique_ptr` |
| 只是数一数"有没有泄漏" | 重载 `operator new`/`delete` 计数（本文 §4 的做法），比装工具还快 |
| 想知道"这地址在哪一段" | 读 `/proc/self/maps`（本文 §4 的 `where()`） |
| 嵌入式（STM32）里要不要担心？ | 栈/静态是确定的；**裸 `malloc`/`new` 会带来碎片化**，所以 MCU 上优先静态分配 + 对象池 —— 这条对以后写固件很有用 |

---

## 6. 正确 ✗ 错误 对照（对着我 README 里的原话）

| 我原来的说法 | 判定 | 正确说法 |
|---|---|---|
| "为什么不直接把临时指针变成堆区的指针？" | ❌ 前提错 | 堆只保证"**不会自动回收**"，不保证"**不会被回收**"：没人 delete = **泄漏**（实测 4b：构造 +1 / 析构 +0） |
| "如果做成堆区的指针，就会造成内存浪费" | 🟡 结论对、理由错 | 真正理由：**所有权不明确**（谁 delete？）+ **异常安全差**；而 `make_shared` 反而更省（1 次 vs 2 次分配） |
| "只需要一个智能指针指向 publish 这类类就行了" | ✅ 对 | **Publisher 本体一个就够**；但要多份 `shared_ptr` 副本也**不会**多建对象（实测额外分配 0 次） |
| "如果有两个智能指针……我的 publish 依然存在" | 🟡 条件成立 | 成立的前提是"**还有持有者**"（如 lambda 捕获，见 `07` 篇），**不是因为它在堆上** |
| "new 的内存开辟是连续的空间" | 🟡 半对 | **一块内部连续**；**块之间不连续**（实测相邻差 80 ≠ sizeof 64，中间是 chunk 头 + 对齐） |
| "如果 node 被销毁，内部的智能指针内存也会被 delete" | 🟡 半对 | 成员**自己**会析构；成员指向的**对象**要等引用计数归零（见 `08` 篇） |

---

## 7. 口诀 & 排查清单

### 口诀

> **"位置管回收，计数管生死，分配器管地址。"**
> 三件事互不决定 —— 混在一起想，怎么推都别扭。

### 排查清单

| # | 问题 | 怎么看 |
|:---:|---|---|
| 1 | 这块内存在哪个段？ | 读 `/proc/self/maps`（§4 `where()`） |
| 2 | 分配了几次、多大？ | 重载 `operator new` 打印（§4） |
| 3 | 有没有泄漏？ | 构造/析构计数对比（§4 的 `g_ctor - g_dtor`） |
| 4 | 谁在持有这个对象？ | `use_count()` |
| 5 | "它怎么还没死 / 怎么早死了？" | 先分清楚你问的是**成员本身**还是**成员指向的对象** |

---

## 8. 关联知识（下次直接跳这里）

| 项 | 内容 |
|---|---|
| 复现代码 | §4（`/tmp/w1_alloc_demo.cpp`，重启会清，故已内嵌） |
| 相关笔记 | `08_对象内存布局与析构时机.md`（成员内嵌 / 偏移 / 引用计数）、`07_句柄与智能指针生命周期.md`（不接住就消失）、`06_引用与const引用_左值与右值.md`（"不拷贝"的正确做法） |
| 本次涉及的源码 | `~/ros2_study_myself/w1_study_workspace/src/w1_topic_demo/src/topic_publisher_demo.cpp:11,20` |
| 命令/文件 | `/proc/self/maps`（地址→段）、`malloc_usable_size()`（`<malloc.h>`，实际可用字节）、重载 `operator new`/`delete` |
| 迁移到 STM32 时的提醒 | MCU 上避免频繁 `malloc`/`new`（碎片）；优先静态/池分配 —— 这也是后面写 micro-ROS 固件时要守的纪律 |

---

## 9. 一句话复述（自测用，能背下来才算过）

> 我把三件事分开：**位置**（栈/堆/静态）只决定"这块存储何时被回收"，
> **引用计数**只决定"被指对象何时被析构"，**分配器**决定"地址连不连续（不连续）"；
> `make_shared` 只分配 1 次、`shared_ptr(new T)` 分配 2 次；
> 而"把智能指针搬到堆上"**不是**让对象安全存活的办法 —— 没人 delete 就是**泄漏**，
> 正确的做法是**把它交给一个生命周期正确的所有者**。

---

*笔记结束 ｜ 2026-09-20*



