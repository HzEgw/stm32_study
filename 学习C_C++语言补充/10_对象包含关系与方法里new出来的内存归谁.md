# 10 · 对象的「包含关系」与「引用关系」—— 方法里 `new` 出来的内存归谁？

> **记录时间**：2026-09-20
> **触发场景**：把 `09` 读完后，把问题问到了最精确的一句：
> **"堆区 a 调用了一个方法，方法里面又使用了 `new`，那我 `new` 出来的内存还属于 a 吗？"**
> **原始疑问（自己 README 原文 + 补充说明）**：
> > "是不是一个对象的内部的所有内存都是连续的，即使调用了父类的方法？
> > 好比我的 publish 是调用 NODE 的方法生成的，那么 publish 是不是存在于我开辟 node 对象的内存当中？
> > ……我不知道**堆区里面再开辟堆区合法吗**……如果 `new` 出来的不属于 a，
> > 那么 node 对象被销毁了，我的 publish 类还会存在，反之不会。"
> **对应源码**：`~/ros2_study_myself/w1_study_workspace/src/w1_topic_demo/src/topic_publisher_demo.cpp:11`

---

## 1. 一句话结论

1. **对象内存 = 它的"子对象"**（基类子对象 + 成员变量 + vptr），全部**内嵌在同一块分配**里、按偏移连续排布
   （实测 `sizeof(MyNode)=88 = 32(基类) + 32(成员) + 8(裸指针) + 16(智能指针)`）。
2. **方法是"代码"，不是"内存"**。调用方法**不产生、也不搬运**任何对象内存 ——
   父类的成员**本来就在对象内部**（作为基类子对象），跟"调用了谁的方法"毫无关系。
3. **方法里 `new` 出来的是另一块独立分配**：**堆里再开堆完全合法**（而且是常态），
   但新块**不属于**调用它的那个对象 —— 两者只有**引用关系**（对象里存了个地址），**没有包含关系**。
4. **"即将回传的临时智能指针"住在栈上**（16 字节，由"声明在哪"决定），**不在** node 的堆块里；
   它**指向**的 Publisher 才在堆上。**"指针本体"和"被指对象"永远分属两地。**
5. 所以"node 销毁后 Publisher 还在不在"**与内存位置无关**，只看**还有没有别的持有者**（引用计数）。

---

## 2. 实测（原样保留，便于以后搜索）

```
=== 1) 基类子对象也在【同一个对象内部】（连续） ===
      sizeof(MyNode)=88  sizeof(Base)=32
      node 起始           = 0x5800add6f2d0  (偏移 0)
      &node->base_member  = 0x5800add6f2d0  (偏移 0)   ← 基类成员也在对象内部
      &node->my_member    = 0x5800add6f2f0  (偏移 32)
      &node->raw_         = 0x5800add6f310  (偏移 64)
      &node->sp_          = 0x5800add6f318  (偏移 72)
      (Base*)node == node ? 1（基类子对象在偏移 0）

=== 2) 方法里 new 出来的内存在哪？属于调用者吗？ ===
      node 对象本体        0x5800add6f2d0 → [heap]
      node->raw_ 指向的对象 0x5800add6f390 → [heap]
      node->sp_  指向的对象 0x5800add6f340 → [heap]
      地址差 |raw_ - node| = 192 字节 → 两块互不相干的独立内存

=== 3) “即将回传的那个临时智能指针”住在哪个段？ ===
      [方法内] 工厂局部变量 sp 自己的地址 = 0x7ffcc7dc1a30（在栈上）
      [工厂] 返回前：Payload=0x5800add6f5d0，工厂局部 sp=0x7ffcc7dc1aa0
      调用方的 sp_local    0x7ffcc7dc1aa0 → [rw-p] ... [stack]     ← 智能指针本体在栈上
      sp_local 指向的 Payload 0x5800add6f5d0 → [rw-p] ... [heap]   ← 被指对象在堆上

=== 4) 同一个对象里，两种成员的结局不同 ===
      [Publisher 析构] this=0x5800add6f340          ← sp_ 指向的：node 销毁时跟着死
      node 销毁后：构造 +0 / 析构 +1
      裸指针指向的对象(仍在堆) 0x5800add6f390 → [heap]  ← raw_ 指向的：还活着（泄漏）
      手动 delete 后：构造 3 次 / 析构 3 次            ← 它一直都在，只是没人能访问到它
```

---

### 2.7 ★ 在**真实的 rclcpp 类型**上量一遍（决定性证据）

程序：`/tmp/w1_handle_ws/src/handle_demo/src/where_is_publisher.cpp`（节点类里只放一个 `pub_` 成员），
关键三行就是：

```cpp
std::printf("&pub_      = %p (偏移 %ld)\n", (void*)&pub_, (long)((char*)&pub_ - (char*)this));
std::printf("pub_.get() = %p\n", (void*)pub_.get());                    // Publisher 本体
std::printf("sizeof(*this)=%zu sizeof(pub_)=%zu sizeof(Publisher)=%zu\n",
            sizeof(*this), sizeof(pub_), sizeof(rclcpp::Publisher<String>));
```

实测输出：

```
node 对象本体 (this)          = 0x55b343ab47f0   → [heap]
&pub_  （成员 shared_ptr）    = 0x55b343ab4b70   (偏移 896，在 node 内部) → [heap] ← 同一块
pub_.get()（Publisher 本体）   = 0x55b343d084b0   ← 另一块内存！            → [heap] ← 不同块
差值 |本体 - node|             = 2440384 字节（≈ 2.4 MB）
sizeof(*this) = 912    sizeof(pub_) = 16
sizeof(rclcpp::Publisher<String>) = 488    ← 本体 488 字节，但【不进】node 的 sizeof
pub_.use_count() = 1                        ← 唯一持有者
main 里 node 变量本身 = 0x7ffd26aa0df0 → [stack]  ← 栈上的那个 shared_ptr 才是持有者
```

| 数字 | 说明 |
|---|---|
| `&pub_` 偏移 **896** < `sizeof(*this)` **912** | 成员 `pub_` **确实在 node 对象内部**（896+16=912，正好是最后 16 字节） |
| Publisher 本体离 node **≈2.4 MB** | 它是**独立另一块堆内存**，与 node 没有"包含"关系 |
| `sizeof(pub_)=16` vs `sizeof(Publisher)=488` | node 里**只有两个指针**；本体那 488 字节**完全不在** node 里 |

> 📌 这就是"**node 里保存的只是引用，Publisher 实体在另一块独立内存**"的最终实证 ——
> 而且用的是**你自己的 `rclcpp::Node` / `create_publisher`**，不是玩具模型。

---

## 3. 为什么会这样（原理）

### 3.1 对象内部到底有谁？（实测偏移）

```
        make_shared<MyNode>() 分配的【一块】堆内存（88 字节）
        ┌──────────────────────────────────────────────────────────────────┐
MyNode* │ Base 子对象(32)  │ my_member(32) │ raw_(8) │ sp_(16)              │
        │  └ base_member   │               │         │  = 对象指针 + 控制块指针 │
        └───偏移 0──────────偏移 32────────偏移 64──偏移 72─────────────────┘
```

| 结论 | 说明 |
|---|---|
| **基类子对象在偏移 0** | `(Base*)node == node`（实测 1）→ 基类部分**不是**另开一块，而是**嵌在派生类对象里最前面** |
| 成员按声明顺序排 | `my_member`(32) → `raw_`(64) → `sp_`(72) |
| `sizeof` = 子对象之和 + 对齐 | 88 = 32+32+8+16 ✅ |
| **方法不占对象内存** | 方法是**代码**（代码段）；它的**局部变量在栈上**（实测 §2.3 里 `&sp = 0x7ffc…`） |

### 3.2 「包含关系」vs「引用关系」（这张图解释一切）

```
   node 对象（一块堆内存，88 字节）
   ┌─────────────────────────────────┐
   │ Base 子对象 │ my_member │ raw_ │ sp_ │           ← raw_/sp_ 只是【16 或 8 字节的地址】
   └─────────────────────────────────┘
                                  │      │
                    引用（指针）───┘      └───引用（指针）
                                  ▼           ▼
                        ┌──────────────┐  ┌──────────────┐
                        │ Publisher A  │  │ Publisher B  │   ← 各自独立的堆块
                        └──────────────┘  └──────────────┘
```

- **包含**（内嵌）：基类子对象、成员变量 → **同属一块分配**，随宿主一起生灭；
- **引用**（指针）：`raw_` / `sp_` 里存的**只是地址** → 被指对象在**别处**，生灭由**持有关系**决定。

> ⚠️ 所以"**publish 是不是存在于我开辟 node 对象的内存当中**"——**不是**。
> node 的那块内存里，关于 publish 只有 **16 个字节的地址信息**。

### 3.3 "堆区里再开辟堆区"合法吗？

**完全合法**，而且是标准做法。所以"堆里再开堆"这件事本身没有任何特殊性：

| 疑问 | 事实 |
|---|---|
| 从堆上的对象 A 调方法，方法里 `new` —— 合法吗？ | ✅ 合法。`new` 只是**再向分配器要一块**，跟"当前在哪块内存上执行"无关 |
| 新块"属于" A 吗？ | ❌ 不属于。**两块内存没有任何归属/包含关系**，A 里最多存一个**指向它的地址** |
| 它们会落在同一个段吗？ | ✅ 都在 `[heap]` 段里（实测三次 `where()` 全是 `[heap]`）——**同段 ≠ 同一块**，地址差 192 字节 |
| 谁负责 `delete` 新块？ | **跟 A 一起不会被自动回收**：要么 A 里存着 `shared_ptr`（计数归零时回收），要么**泄漏** |

### 3.4 "临时智能指针"到底住在哪？

**由"你把它声明在哪"决定** —— 实测：

| 位置 | 实测地址 | 段 |
|---|---|---|
| 工厂（方法）里的局部变量 `sp` | `0x7ffcc7dc1a30` | `[stack]` |
| 调用方接住的 `sp_local` | `0x7ffcc7dc1aa0` | `[stack]` |
| 它**指向**的 Publisher | `0x5800add6f5d0` | `[heap]` |

而且实测：**工厂返回的地址 = 调用方的地址（`0x7ffcc7dc1aa0` 完全相同）** →
**C++17 起，按值返回的对象直接在"调用方的存储"里构造（保证的复制消除）**，根本没有"堆上的临时智能指针"这回事。
**指针本体在栈、被指对象在堆**，这条永远成立。

### 3.5 决定生死的从来不是"位置"，而是"持有关系"（实测 §2.4）

同一块 node 内存里的两个成员，指向的两个 Publisher**都在堆里**，结局却相反：

| 成员 | 持有方式 | node 销毁时 | 结果 |
|---|---|---|---|
| `sp_`（`shared_ptr`） | **有人拥有** | 成员析构 → 计数 -1 → 归零 | **Publisher 立刻析构**（实测打印了它的析构） |
| `raw_`（裸指针） | **没人拥有** | 成员析构只是丢掉地址 | **Publisher 仍活着**（实测仍在 `[heap]`；最后手动 `delete` 才析构） |

> 你那句推论 —— **"如果 `new` 出来的不属于 a，那么 node 被销毁后 publish 还会存在"** ——
> **方向是对的**！但更准确的说法是：**能不能"存在"，取决于还有没有别的持有者**；
> 在 rclcpp 里，**你是唯一持有者**（`pub_` 是成员），所以 node 死 → 计数归零 → Publisher 也死；
> 但如果**有人复制了那个 `shared_ptr`**（比如 lambda 按值捕获，见 `07` 篇实测），**它就能活过 node**。

---

## 4. 最小复现（不依赖 ROS，可独立编译运行）

```cpp
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

static int g_ctor = 0, g_dtor = 0;

struct Payload {                                   // 模拟 rclcpp::Publisher
    long pad[8];
    Payload()  { ++g_ctor; std::printf("   [Publisher 构造] this=%p\n", (void*)this); }
    ~Payload() { ++g_dtor; std::printf("   [Publisher 析构] this=%p\n", (void*)this); }
};

struct Base {                                      // 模拟 rclcpp::Node（基类）
    long base_member[4];
    void make_payload(Payload ** raw_out, std::shared_ptr<Payload> & sp_out)   // 模拟 create_publisher
    {
        auto sp = std::make_shared<Payload>();     // 独立堆块 ①
        std::printf("   [方法内] 局部 sp 自己的地址 = %p（栈）\n", (void*)&sp);
        *raw_out = new Payload();                  // 独立堆块 ②
        sp_out = sp;
    }
};

struct MyNode : Base {                             // 模拟我们自己的节点类
    long                     my_member[4];
    Payload *                raw_{nullptr};
    std::shared_ptr<Payload> sp_;
};

static void where(const char * tag, const void * addr)          // 地址落在哪个段
{
    std::ifstream f("/proc/self/maps"); std::string line; unsigned long a = (unsigned long)addr;
    while (std::getline(f, line)) {
        unsigned long lo = 0, hi = 0; char p[8] = {0}, rest[512] = {0};
        if (std::sscanf(line.c_str(), "%lx-%lx %7s %511[^\n]", &lo, &hi, p, rest) >= 3
            && a >= lo && a < hi) {
            std::printf("   %-22s %p → [%s] %s\n", tag, addr, p, rest); return;
        }
    }
}

static std::shared_ptr<Payload> factory()          // 模拟 create_publisher 的返回
{
    auto sp = std::make_shared<Payload>();
    std::printf("   [工厂] Payload=%p  工厂局部 sp=%p\n", (void*)sp.get(), (void*)&sp);
    return sp;                                     // C++17：直接构造在调用方存储里
}

int main()
{
    auto node = std::make_shared<MyNode>();
    auto * b  = reinterpret_cast<unsigned char *>(node.get());
    std::printf("sizeof(MyNode)=%zu  sizeof(Base)=%zu\n", sizeof(MyNode), sizeof(Base));
    std::printf("base_member 偏移 %ld  my_member %ld  raw_ %ld  sp_ %ld\n",
        (long)((unsigned char*)&node->base_member - b), (long)((unsigned char*)&node->my_member - b),
        (long)((unsigned char*)&node->raw_ - b),        (long)((unsigned char*)&node->sp_ - b));
    std::printf("(Base*)node == node ? %d\n",
        static_cast<void*>(static_cast<Base*>(node.get())) == static_cast<void*>(b));

    node->make_payload(&node->raw_, node->sp_);    // 方法里 new 出两块独立内存
    where("node 本体", node.get());
    where("raw_ 指向的对象", node->raw_);
    where("sp_  指向的对象", node->sp_.get());

    { auto sp_local = factory();                   // “即将回传的临时智能指针”落在哪
      where("调用方的 sp_local", &sp_local);
      where("它指向的 Payload", sp_local.get()); }

    { Payload * keep = node->raw_;                 // 同一对象里两种成员的结局
      int c = g_ctor, d = g_dtor;
      node.reset();                                // 模拟：节点被销毁
      std::printf("node 销毁后：构造 +%d / 析构 +%d\n", g_ctor - c, g_dtor - d);
      where("裸指针指向的对象(仍在堆)", keep);
      delete keep; }                               // 手动收尾：证明它一直都在
    return 0;
}
```

```bash
g++ -std=c++17 -Wall -Wextra -o /tmp/w1_contain_demo /tmp/w1_contain_demo.cpp && /tmp/w1_contain_demo
```

实测输出见本文 **§2**。

---

## 5. 要不要 / 什么时候用（判断标准）

| 想表达的关系 | 怎么写 | 结果 |
|---|---|---|
| "**它就是我的一个部件**"（同生共死、内嵌） | 成员用**值**：`long x_; std::string s_;` | 在对象内部，`sizeof` 包含它，随宿主生灭 |
| "**它独立存在，我只是用一下**" | 成员用**引用/裸指针**（且**必须**由别人负责它的生死） | 对象里只存地址 |
| "**我和别人共同拥有它**"（谁先走都行） | 成员用 `shared_ptr`（rclcpp 的 `pub_`/`sub_`/`tim_`） | 计数归零才析构 |
| "**我独占它**" | 成员用 `unique_ptr` | 宿主销毁即销毁 |
| 方法里要"造一个东西交给调用方" | 返回**值**或**智能指针**（别返回裸 `new` 的指针 —— 所有权不明） | rclcpp 的 `create_*` 就是这么设计的 |

> ⚠️ **最危险的一类**：成员是裸指针 `T * p_;` 却在构造函数里 `new`。
> 宿主析构时**不会**帮你回收（实测 §2.4 的 `raw_`），要么自己写析构函数，要么改用智能指针。

---

## 6. 正确 ✗ 错误 对照（对着我自己的原话）

| 我原来的理解 | 判定 | 正确说法 |
|---|---|---|
| "一个对象内部的所有内存都是连续的" | ✅ 对自己的**子对象**成立 | 基类子对象 + 成员内嵌、偏移连续（实测 0/32/64/72，`sizeof=88`） |
| "即使调用了父类的方法（也连续）" | 🟡 概念错位 | **方法不是内存**：调用方法不产生/不搬运任何对象内存；父类成员**本来就在**对象内部 |
| "publish 是调用 NODE 的方法生成的，所以存在于 node 的内存当中" | ❌ | `create_publisher` 内部 `make_shared` 是**另一次独立分配**；node 里只有 **16 字节的地址信息** |
| "不知道堆区里面再开辟堆区合法吗" | ✅ 合法 | 常态做法；`new` 与"当前在哪块内存上执行"无关 |
| "`new` 出来的还属于 a 吗" | ❌ 不属于 | 只有**引用关系**；谁 `delete` 要**单独约定**（智能指针负责） |
| "那个临时智能指针处于堆区、可能在 node 的堆区里" | ❌ 两处都错 | 它在**栈**上（实测 `0x7ffc…[stack]`），而且实测**就构造在调用方的变量里** |
| "`new` 出来的不属于 a → node 销毁后 publish 还会存在" | ✅ **方向正确** | 更准确：取决于**还有没有别的持有者**；rclcpp 里你是唯一持有者 → 通常跟着死；有人复制了 `shared_ptr` → 能活过 node（`07` 篇实测） |

---

## 7. 口诀 & 排查清单

### 口诀

> **"内嵌的随宿主生灭，指针指向的在别处；方法不是内存，`new` 永远是另开一块。"**

### 排查清单

| # | 问题 | 怎么看 |
|:---:|---|---|
| 1 | 这块内存在对象的哪一段？ | 打印 `&obj.member - (char*)&obj`（偏移小 = 内嵌） |
| 2 | 这个对象总共多大、都有谁？ | `sizeof` + 各子对象偏移，看是否等于各成员之和 |
| 3 | 方法里 `new` 的东西在哪？ | 打印它的地址，与 `&obj` 比较（差很多 = 另一块） |
| 4 | 它在哪个段？ | 读 `/proc/self/maps`（`09` 篇的 `where()`） |
| 5 | 宿主销毁后它还活着吗？ | 看**持有方式**：成员是值 / 智能指针 / 裸指针？ |
| 6 | 会不会泄漏？ | 构造/析构计数对比（`09` 篇 §4） |

---

## 8. 关联知识（下次直接跳这里）

| 项 | 内容 |
|---|---|
| 复现代码 | §4（`/tmp/w1_contain_demo.cpp`，重启会清，故已内嵌） |
| 相关笔记 | `08_对象内存布局与析构时机.md`（成员内嵌 / 偏移 / 引用计数）、`09_内存分配到底怎么回事_栈堆与new.md`（栈/堆/静态、分配器、泄漏）、`07_句柄与智能指针生命周期.md`（句柄必须被接住） |
| 本次涉及的源码 | `~/ros2_study_myself/w1_study_workspace/src/w1_topic_demo/src/topic_publisher_demo.cpp:11,12,20-22` |
| 标准依据 | C++17 **保证的复制消除**（按值返回的对象直接构造在调用方存储里；实测工厂地址 == 调用方地址） |
| 命令 | `/proc/self/maps`（地址→段）；`sscanf("%lx-%lx %7s %511[^\n]")` 解析 |

---

## 9. 一句话复述（自测用，能背下来才算过）

> 我记住了：**对象内部只有"子对象"是内嵌的**（基类子对象 + 成员），**方法是代码、不占对象内存**；
> 方法里 `new` 出来的东西**永远是另开一块**，和调用者只有**引用关系**、**没有包含关系**；
> 而那个"临时智能指针"**住在栈上**、指向的对象在堆上 ——
> 所以"node 死了 publish 还在不在"**不能看内存位置，只能看还有没有别的持有者**。

---

*笔记结束 ｜ 2026-09-20*

