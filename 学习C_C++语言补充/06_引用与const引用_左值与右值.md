# 06 · 引用、`const` 引用与左值/右值 —— `&` 和 `const&` 到底是不是两回事？

> **记录时间**：2026-09-20
> **触发场景**：写 W1 订阅者时只会照抄 `const StringDate & msg`，说不清它和 `&`、和指针的关系
> **原始疑问（自己 README 里的原文）**：
> > "我用到了 `const &`，虽然我知道这是可以引用左值和右值，但是我代码或许写的太少了，
> > 我不明白这两个的区别还有什么…… 我一直觉得 [引用] 和指针类似……
> > 但是 `const &` 又可以引用右值，所以我是否应该把两个概念分开？"
> **对应源码**：`~/ros2_study_myself/w1_study_workspace/src/w1_topic_demo/src/w1_subscriber_demo.cpp:11`

---

## 1. 一句话结论

**不是"两回事"，而是三个正交概念被挤在同一个符号里：**

| 写法 | 实际上是哪几件事拼起来的 |
|---|---|
| `T&` | 引用（别名）+ 绑左值 |
| `const T&` | 引用 + **只读** + **可以**绑右值（临时对象） |
| `T*` | 指针（独立变量）+ 可空 |
| `T&&` | 右值引用（移动语义，先认识名字即可） |

> "传地址 / 省一次拷贝"是**引用和指针共有**的效果，**跟 `const` 无关**；
> `const` 只管"**只读**"；"能不能绑右值"由**左值/右值**这条轴决定。

---

## 2. 实测（原样保留，便于以后搜索）

### 2.1 引用没有"自己"（`/tmp/w1_ref_demo.cpp` 实跑）

```
&a            = 0x7fff9c5231c0   ← 对象本身地址
p(指针的值)   = 0x7fff9c5231c0   ← 指针里存的地址 = 对象地址
&p(指针自己)  = 0x7fff9c523190   ← 指针变量自己另占一块栈空间
&r(对引用取址)= 0x7fff9c5231c0   ← 拿到的是【被引用对象】的地址，不是引用的
sizeof(a)=32  sizeof(r)=32  ← 引用“没有自己”
sizeof(p)=8   ← 指针自己就占 8 字节
```

### 2.2 非常量引用绑右值 → 编译错误原文

```
error: cannot bind non-const lvalue reference of type 'std::string&'
       {aka 'std::__cxx11::basic_string<char>&'} to an rvalue of type 'std::string'
```

### 2.3 返回临时对象的引用 → 编译警告原文

```
warning: returning reference to temporary [-Wreturn-local-addr]
    2 | static const T & f(){ return T{42}; }
      |                              ^~~~~
```

---

## 3. 为什么会这样（原理）

### 3.1 三条正交的轴

| 轴 | 取值 | 决定什么 |
|---|---|---|
| ① 实体形态 | 引用 `&` / 指针 `*` | 有没有独立实体、可否为空、可否改绑 |
| ② 只读性 | `const` / 非 `const` | 能否通过它修改被指对象 |
| ③ 值类别 | 左值 / 右值 | **谁能绑上它** |

### 3.2 引用 vs 指针

| 项 | `T& r = a;` | `T* p = &a;` |
|---|---|---|
| 是否独立变量 | 否（只是别名） | 是（自己占 8 字节） |
| 取它自己的地址 | 做不到（`&r` 是对象地址） | `&p` 可以 |
| 能否为空 | 不能，必须绑真实对象 | 能（`nullptr`） |
| 能否改绑 | 不能，一绑终身 | 能（`p = &b`） |
| 算术 / 遍历 | 不能 | 能（`p++`） |
| `sizeof` | = 被引用对象的 sizeof | = 指针的 sizeof（8） |
| 传参开销 | 不拷贝对象 | 不拷贝对象 |

### 3.3 `const&` 为什么能绑右值

右值（临时对象）**没有名字、马上要死**，所以标准规定：只有 `const` 引用与右值引用 `T&&` 能绑它。
绑上之后还有**生命周期延长**规则：**直接**用 `const T&` 绑一个临时对象时，
该临时对象的寿命被延长到**这个引用离开作用域**（实测见 §4 的构造/析构打印）。

⚠️ 但延长**不是万能的**：如果那个 `const&` 是从函数**返回**出去的，延长不成立 → 悬空引用（§2.3）。

### 3.4 组合表（背这张就够）

| 写法 | 绑左值 | 绑右值 | 可改对象 | 典型用途 |
|---|:---:|:---:|:---:|---|
| `T&` | ✅ | ❌ | ✅ | 要修改调用方的对象 |
| `const T&` | ✅ | ✅ | ❌ | **只读参数的默认选择**（ROS2 回调就是它） |
| `T*` | ✅ | ✅ | ✅ | 可为空 / 改指向 / 遍历 |
| `const T*` | ✅ | ✅ | ❌ | 可为空 + 只读 |
| `T&&` | ❌ | ✅ | ✅ | 移动语义（`std::move`） |

---

## 4. 最小复现（不依赖 ROS，可独立编译运行）

```cpp
#include <cstdio>
#include <string>

struct Tracker {                        // 只为“看见”临时对象的构造/析构时机
    const char * tag;
    explicit Tracker(const char * t) : tag(t) { std::printf("      [构造] %s\n", tag); }
    ~Tracker() { std::printf("      [析构] %s\n", tag); }
};

static std::string make_name() { return std::string("临时对象"); }
static void take_lref(std::string & s) { std::printf("    take_lref(string&)      可改原对象 → %s\n", s.c_str()); s += "（被改过了）"; }
static void take_cref(const std::string & s) { std::printf("    take_cref(const string&) 只读     → %s\n", s.c_str()); }

int main()
{
    std::printf("=== 1) 引用不是指针 ===\n");
    std::string a = "左值字符串";
    std::string & r = a;      // 一绑终身
    std::string * p = &a;     // 独立变量，可改指向
    std::printf("    &a=%p  p(指针的值)=%p  &p(指针自己)=%p  &r=%p\n",
                (void*)&a, (void*)p, (void*)&p, (void*)&r);
    std::printf("    sizeof(a)=%zu sizeof(r)=%zu sizeof(p)=%zu\n", sizeof(a), sizeof(r), sizeof(p));

    std::printf("=== 2) 非常量引用只能绑左值；const& 还能绑右值 ===\n");
    take_lref(a);
    take_cref(make_name());                 // const& 直接吃临时对象

    std::printf("=== 3) const& 绑临时对象：生命周期被延长 ===\n");
    {
        const Tracker & t = Tracker("被延长的临时对象");
        std::printf("    t.tag = %s\n", t.tag);
    }                                       // ← 析构在这里，不在绑定那一行
    return 0;
}
```

实测输出（关键三行）：

```
=== 1) 引用不是指针 ===
    &a=0x7fff9c5231c0  p(指针的值)=0x7fff9c5231c0  &p(指针自己)=0x7fff9c523190  &r=0x7fff9c5231c0
    sizeof(a)=32 sizeof(r)=32 sizeof(p)=8
=== 3) const& 绑临时对象：生命周期被延长 ===
      [构造] 被延长的临时对象
    t.tag = 被延长的临时对象
      [析构] 被延长的临时对象
```

---

## 5. 要不要 / 什么时候用（判断标准，别背结论）

1. **函数入参默认 `const T&`**：只读 + 不拷贝 + 能吃临时值。
   你自己的 `const StringDate & msg` 就是这个用法，**实测回调正常收到消息（打印 3 次）**。
2. **要改调用方的对象** → `T&`（如 `void sort(std::vector<int> & v)`）。
3. **需要"没有对象"这个状态 / 要改指向 / 要遍历** → 指针。
4. **返回值**：**不要**返回临时对象的引用（§2.3 的警告）；返回对象值，或返回持有者（`shared_ptr`）。
5. **成员变量一般别用引用**（`T& m_;`）：类会变得不能赋值、初始化也麻烦；用值或智能指针。

---

## 6. 正确 ✗ 错误 对照

| ✗ 错误 | ✅ 正确 | 说明 |
|---|---|---|
| `void f(std::string & s)` 却传字面量 `f("abc")` | `void f(const std::string & s)` | 非常量引用不能绑右值 |
| `void f(std::string s)` 传大对象 | `const std::string &` | 省一次拷贝 |
| `const T& f(){ return T{...}; }` | 返回 `T`（值） | 返回临时对象的引用 = 悬空 |
| 以为 `const&` 会拷贝对象 | 知道它**不拷贝** | `const` 只管只读，不管拷贝 |
| 以为引用是"传地址"的语法糖、占 8 字节 | 引用是别名（`sizeof` 可证伪） | 见 §2.1 |
| 以为"引用和 `const&` 是两个要分开的概念" | 分成**三条轴**看 | 见 §3.1 |

---

## 7. 口诀 & 排查清单

### 口诀

> **"引用不占位，const 管只读，左右值管能不能绑。"**

### 排查清单

| # | 现象 | 先查什么 |
|:---:|---|---|
| 1 | `cannot bind non-const lvalue reference ... to an rvalue` | 参数改成 `const T&` |
| 2 | `returning reference to temporary` | 别返回临时对象的引用 |
| 3 | 分不清"引用 / 指针" | 打印 `&r`、`&p`、`sizeof(...)`（§2.1 三行就是判据） |
| 4 | 不确定能不能传临时值 | 看参数是不是 `const&`（只有它和 `T&&` 能吃临时值） |

---

## 8. 关联知识（下次直接跳这里）

| 项 | 内容 |
|---|---|
| 复现代码 | 见本文 §4（`/tmp` 重启会清，故已内嵌） |
| 本机验证文件 | `/tmp/w1_ref_demo.cpp`（引用对照）、`/tmp/w1_e1.cpp`（错误原文）、`/tmp/w1_e2.cpp`（警告原文） |
| 相关笔记 | `07_句柄与智能指针生命周期.md`、`05_shared_from_this与bad_weak_ptr.md` |
| ROS2 侧同类用法 | `create_subscription<T>(话题, 队列, [](const T & msg){ ... })` —— 回调参数就是 `const T&` |
| 本次涉及的源码 | `~/ros2_study_myself/w1_study_workspace/src/w1_topic_demo/src/w1_subscriber_demo.cpp:11` |

---

## 9. 一句话复述（自测用，能背下来才算过）

> 我会把"引用 / 指针"（实体形态）、"`const`"（只读性）、"左值 / 右值"（可绑性）当成**三条独立的轴**；
> `const T&` = 只读别名 + 能绑临时对象 + 延长其寿命；
> 而"少一次拷贝"是引用与指针**共有**的效果，跟 `const` 无关。

---

*笔记结束 ｜ 2026-09-20*
