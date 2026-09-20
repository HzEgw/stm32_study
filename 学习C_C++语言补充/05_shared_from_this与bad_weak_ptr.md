# 05 · `shared_from_this()` 与 `std::bad_weak_ptr`

> **记录时间**：2026-09-20
> **触发场景**：自己动手写 W1 话题发布者时，在构造函数里加了"自指针"
> **对应源码**：`~/ros2_study_myself/w1_study_workspace/src/w1_topic_demo/src/topic_publisher_demo.cpp` 第 12~13 行
>
> **📌 归档说明（2026-09-20 二次归类）**
> 这篇最初被放进 ROS2 笔记（`~/ros2_study_myself/学习笔记/`），
> 但按分类规则它属于 **C / C++ 语言问题** —— 根因是 `std::enable_shared_from_this`
> 与 `std::bad_weak_ptr`，是**纯 C++ 标准库**机制，与 ROS2 无关
> （本文第 4 节的最小复现**完全不依赖 ROS**）。
> **触发场景在 ROS2 ≠ 问题属于 ROS2**，故移入本文件夹，编号 `05`。

---

## 1. 一句话结论

**`shared_from_this()` 不能在构造函数里调用。**

它内部靠一个**弱引用**（`weak_this`）工作，而这个弱引用是 `shared_ptr`
**在对象构造完成之后**才登记进去的 —— 在构造函数里调用时它还是空的，
所以**必然**抛 `std::bad_weak_ptr`。

**附加结论（同样重要）**：在 `main()` 常驻节点的场景下，
那个"把 self 存成成员 / 捕获进 lambda"的**自指针根本不需要**，直接捕获 `this` 就够。

---

## 2. 现象（报错原文，原样保留便于以后搜索）

```
topic_publisher_node
terminate called after throwing an instance of 'std::bad_weak_ptr'
  what():  bad_weak_ptr
[ros2run]: Aborted
```

当时的疑惑：

> 我外面明明用的是 `std::make_shared`（共享指针），
> 为什么报错里说的是"**弱**引用"？我不是设置的是共享指针吗？

---

## 3. 为什么会这样（原理）

### 3.1 `enable_shared_from_this` 内部其实存的是**弱引用**

```cpp
template<class T>
class enable_shared_from_this {
protected:
    mutable std::weak_ptr<T> weak_this;       // ← 这里！存的是【弱】引用
public:
    std::shared_ptr<T> shared_from_this() {
        return std::shared_ptr<T>(weak_this); // weak_this 为空 → 抛 bad_weak_ptr
    }
};
```

所以：

- `shared_from_this()` 干的事，本质就是 **`weak_this.lock()`**
- `weak_this` 一旦是空的 → **必然**抛 `std::bad_weak_ptr`
- **异常名里的 "weak" 指的就是这个内部弱引用**，跟你用没用弱指针没关系

### 3.2 `weak_this` 是谁、什么时候填进去的？

由 `shared_ptr` 的构造函数填，**但必须在对象构造完成之后**：

| 步骤 | 发生什么 | `weak_this` 状态 |
|:---:|---|---|
| ① | `make_shared<T>()` 分配内存，**调用你的构造函数** | ❌ **还是空的** ← 就在这里调用了 `shared_from_this()` |
| ② | 构造函数返回 | ❌ 仍空 |
| ③ | `shared_ptr` 构造流程执行 `_M_enable_shared_from_this_with()`，登记 `weak_this` | ✅ 填好了 |
| ④ | 之后任何地方再调用 `shared_from_this()` | ✅ 正常 |

**一句话**：在构造函数里调用，等于"**登记之前就去查档案**"，报错是必然的。

### 3.3 `rclcpp::Node` 为什么也能调它

`/opt/ros/humble/include/rclcpp/rclcpp/node.hpp` **第 77 行**：

```cpp
class Node : public std::enable_shared_from_this<Node>
```

我们的节点类继承自它，所以 `this->shared_from_this()` 走的就是上面这套逻辑。

---

## 4. 最小复现（不依赖 ROS，可独立编译运行）

```cpp
// 机制与 rclcpp::Node 完全相同
#include <cstdio>
#include <memory>

class Base : public std::enable_shared_from_this<Base> {
public:
    Base() = default;
    void call_after_ctor() {                  // 构造函数【之外】调用
        try {
            auto p = shared_from_this();
            printf("  [构造函数之外] OK, use_count = %ld\n", p.use_count());
        } catch (const std::bad_weak_ptr & e) {
            printf("  [构造函数之外] 抛异常: %s\n", e.what());
        }
    }
};

class Derived : public Base {
public:
    Derived() {
        try {                                 // 构造函数【之内】调用
            auto p = shared_from_this();
            printf("  [构造函数之内] OK, use_count = %ld\n", p.use_count());
        } catch (const std::bad_weak_ptr & e) {
            printf("  [构造函数之内] 抛异常: bad_weak_ptr (%s)\n", e.what());
        }
    }
};

int main() {
    auto sp = std::make_shared<Derived>();    // 用的是 std::shared_ptr！
    printf("对象已创建完毕，use_count = %ld\n", sp.use_count());
    sp->call_after_ctor();
    return 0;
}
```

编译运行：

```bash
g++ -std=c++17 -o /tmp/esft_repro /tmp/esft_repro.cpp && /tmp/esft_repro
```

**实测输出**：

```
即将用 std::make_shared<Derived>() 创建对象（std::shared_ptr，不是 weak_ptr）：
  [构造函数之内] 抛异常: bad_weak_ptr (bad_weak_ptr)     ← 同样报错
对象已创建完毕，use_count = 1
现在在构造函数之外再调用一次：
  [构造函数之外] OK, use_count = 2                       ← 构造完就正常
```

**同一个对象、同一个函数，只是调用位置不同，结果天差地别** —— 这就是铁证。

---

## 5. 那个"自指针"到底要不要？（判断标准，别背结论）

### 5.1 结论：**在你这个场景不需要**

动手写代码之前的直觉：

> 外面 `main()` 里已经 `make_shared` 创建了，节点一定活着，
> 再到里面加一个指针，不是没必要吗？

**完全正确**。理由：

- 节点由 `main()` 里的 `auto node` 持有，一直活到 `rclcpp::spin(node)` 返回之后才析构
- 定时器回调**只可能**在 `spin()` 期间被触发，那时节点**必然**活着
- 所以只捕获 `this` 就够了 —— 教程代码与 `ros2_study_with_cline` 里的参考代码
  用的都是 `std::bind(&类::函数, this, ...)`，正是这个道理

### 5.2 而且捕获 self 反而**有害**：引用环

```
 node ──持有──> tim_ ──持有──> lambda ──持有──> shared_ptr<node> ──┐
   ↑                                                              │
   └──────────────────────────────────────────────────────────────┘
```

`node` 的引用计数**永远降不到 0** → **节点永远不会被析构（内存泄漏）**。

短命节点感觉不到，但这是坏习惯 —— 等以后写**长驻节点**（比如 micro-ROS agent 那类）会咬人。

### 5.3 什么时候**才真的**需要 `shared_from_this()`

| 场景 | 说明 |
|---|---|
| 回调可能比外部持有者**活得更久** | 异步 I/O、延时任务，外部 `shared_ptr` 已被 reset |
| 要把节点自身的 `shared_ptr` **交给别人** | 如 `executor->add_node(shared_from_this())`、把 node 存进长期存在的容器 |
| 类内部要给异步链路"续命"自身 | 多线程/异步回调里需要把自己的引用传下去 |

> ⚠️ **即使真的需要，也必须在构造函数之外调用**（单独开一个 `init()` 成员函数、或放到事件回调里）。

---

## 6. 正确 ✗ 错误 对照

### ✗ 错误（本次踩的坑）

```cpp
TopicPublishDemo(const std::string node_name) : Node(node_name)
{
    pub_ = this->create_publisher<StringDate>("/w1/chatter", 10);
    SharedPtr self_node = this->shared_from_this();       // ❌ 构造函数里调用
    tim_ = this->create_wall_timer(1s, [self_node, this]() -> void { /*...*/ });
}
```

三层问题：

1. **构造期调用 `shared_from_this()`** → `bad_weak_ptr`（本次的报错）
2. `self_node` 捕获了却**根本没在 lambda 里用过** → 纯多余
3. 即使能跑通，也会形成**引用环**（见 5.2）

### ✅ 正确（`main()` 常驻节点的惯用写法）

```cpp
tim_ = this->create_wall_timer(
    1s, std::bind(&TopicPublishDemo::on_timer, this));   // ✅ 直接捕获 this
```

---

## 7. 口诀 & 排查清单

### 口诀

> **"构造未完成，无 self 可还。"**
> —— `shared_from_this()` 只能在对象**被 `shared_ptr` 完整接管之后**调用。

### 以后遇到 `bad_weak_ptr` 的排查清单

| # | 检查 |
|:---:|---|
| 1 | 是不是在**构造函数**里调用的？（最常见的原因） |
| 2 | 对象是不是用 `new` 直接创建、**没交给 `shared_ptr`**？（`T* p = new T; p->shared_from_this()` → 报错） |
| 3 | 是不是**栈上对象**？（`T t; t.shared_from_this()` → 报错） |
| 4 | 类是否**同时继承了多个 `enable_shared_from_this`**？（二义性，登记不到你期望的那个） |
| 5 | 回到根本：**到底需不需要它？** —— 常驻节点场景下，九成是"不需要，用 `this` 就行" |

---

## 8. 关联知识（下次直接跳这里）

| 项 | 内容 |
|---|---|
| `rclcpp::Node` 定义 | `/opt/ros/humble/include/rclcpp/rclcpp/node.hpp` 第 **77** 行<br>`class Node : public std::enable_shared_from_this<Node>` |
| `SharedPtr` 从哪来 | 同文件第 **80** 行 `RCLCPP_SMART_PTR_DEFINITIONS(Node)`<br>→ `Node::SharedPtr` 就是 `std::shared_ptr<Node>` |
| 复现代码 | 见本文 **§4**（整段可直接复制保存为 `.cpp` 编译）；验证时用的文件是 `/tmp/esft_repro.cpp`（`/tmp` 重启会清，所以代码已内嵌在 §4） |
| 本次涉及的源码 | `~/ros2_study_myself/w1_study_workspace/src/w1_topic_demo/src/topic_publisher_demo.cpp` 第 12 行 |
| 正确写法对照 | `~/ros2_study_with_cline/src/w1_topic_demo/src/topic_publisher_node.cpp` |

---

## 9. 一句话复述（自测用，能背下来才算过）

> 我不会在**构造函数**里调用 `shared_from_this()`，
> 因为它内部的 `weak_this` 要等**构造函数结束之后**才会被登记；
> 而在**常驻节点**里我根本不需要自指针 —— **捕获 `this` 就够了**，
> 捕获 `self` 反而会造成**引用环**，导致节点永不析构。

---

*笔记结束 ｜ 2026-09-20*

