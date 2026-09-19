# 02 · `static` 在 C 与 C++ 中的用法对照（含类静态成员详解）

> **起因**：我一直模糊记得"静态变量好像只允许在静态函数里修改"。
> 查清后的结论：**这个印象来自 C++/Java 的"类"作用域概念，C 语言里完全没有这回事。**

---

# 第一部分：C 里的 static —— 只改"名字的可见范围"

## 1. 三种用法（按出现的位置区分）

| 位置 | 含义 | 影响的是 |
|---|---|---|
| 文件里的**全局变量**前 | **内部链接**：只有本 .c 能按名字访问 | 可见性 |
| **函数**定义前 | **内部链接**：只有本 .c 能调用 | 可见性 |
| **函数内部**的局部变量前 | 存储期延长到整个程序，只初始化一次 | **生命周期**（和可见性无关！） |

```c
static uint32_t s_cnt = 0;          /* ① 只有本文件能用这个名字 */
static void Helper(void) { }        /* ② 只有本文件能调用 */

void f(void)
{
    static uint32_t calls = 0;      /* ③ 出了函数还在, 下次进函数值还是上次的 */
    calls++;
}
```

⚠️ ③ 最容易搞混：它**不会**让 `calls` 在别的函数里可见，只是"值不会被销毁"。

## 2. static 到底不是访问控制

```c
static uint32_t s_duty[4];          /* 本文件私有 */

static void PrivSet(uint8_t ch, uint16_t d)   /* 可以改 */
{ s_duty[ch] = d; }

void AnyFunc(void)                            /* ❌ 不是 static 的函数 */
{ s_duty[0] = 100; }                          /* ✅ 照样能改! 因为它在同一个 .c 里 */
```

**同一个 .c 里的任何函数都能改 static 变量**——static 只管"别的文件按名字看不见"。
所以"C 里没有 private"这句话是准确的。

## 3. C 里想实现"只允许通过某些函数修改"的正确手法

```c
/* encoder.c */
static int32_t s_position = 0;                  /* ① 数据不外泄 */

int32_t Encoder_GetPosition(void) { return s_position; }   /* ② 只暴露"读" */
void    Encoder_Reset(void)                                 /* ③ 受控的"写" */
{
    s_position = 0;
    TIM_SetCounter(TIM3, 0);        /* 连硬件一起复位, 不允许别人乱写 */
}
```
靠的是 **接口约定 + 链接器**，不是 static 语法。
（并且注意：谁拿到 `TIM3` 指针照样能直接改寄存器，所以 static 也**不是安全机制**。）

---

# 第二部分：C++ 里的 static —— 分 5 种，其中 2 种和"类"有关

| 位置 | 名称 | 含义 | 对应 C 的含义 |
|---|---|---|---|
| 文件作用域的变量/函数前 | 内部链接 | 只有本翻译单元可见 | ✅ 与 C 相同 |
| **类内的数据成员**前 | **静态数据成员** | 属于**类**，不属于对象；所有对象共用一份 | ❌ C 没有类 |
| **类内的成员函数**前 | **静态成员函数** | 没有 `this`，可用 `类名::函数()` 直接调用 | ❌ C 没有类 |
| 函数内的局部变量前 | 静态局部变量 | 生命周期贯穿程序；**C++11 起初始化还是线程安全的**（magic static） | ✅ 与 C 相同（多了线程安全保证） |
| 类内 `inline static`（C++17） | 内联静态成员 | 允许在类内**直接定义**静态成员，省掉类外定义 | ❌ C++17 新增 |

> 你记的"static 成员变量"确实存在 —— **它属于 C++ 的"类"语境**，
> 而我这几个 STM32 工程是纯 C，用的是第 1 种（文件级 static），所以看起来完全不同。

## 4. 类静态数据成员（C++）

```cpp
class Encoder {
public:
    static constexpr uint32_t PPR = 500;      // 类内可直接初始化(常量, C++11 起)
    static uint32_t s_samples;                // 只是"声明"

    void Reset() { s_samples = 0; }           // ✅ 普通成员函数当然能改静态成员
private:
    int32_t m_position = 0;                   // 每个对象各一份(非静态成员)
};

uint32_t Encoder::s_samples = 0;              // ★ 必须在类外定义(且只写一次, 通常放 .cpp)
```

要点清单：

1. **"声明在类里、定义在类外"**：C++17 之前必须这么写，忘了就报
   `undefined reference to 'Encoder::s_samples'`（**最常见的坑**）。
2. **全部对象共享一份**：适合做计数器、共享配置、单例实例。
   ```cpp
   Encoder a, b;
   a.s_samples = 5;        // 合法, 但语义是"类级别"
   Encoder::s_samples;     // 更清楚地表达"我看的是类的东西"
   ```
3. **生命周期**：和程序同寿，构造/析构发生在 `main` 前后（有"静态初始化顺序"陷阱，嵌入式里慎用非 POD 的静态对象）。
4. **常量用途**：`static constexpr uint32_t N = 4;` 可以直接当数组长度、模板参数。
5. C++17 起可写 `static inline uint32_t s_samples = 0;` 省掉类外定义。

## 5. 类静态成员函数（C++）

```cpp
class Encoder {
public:
    static int32_t  CountsPerRev();     // ✅ 只用静态成员, 无 this 也能算
    static Encoder& Instance();         // 单例入口(经典用法)
    static void     TIM3_IRQHandler();  // 中断转发(嵌入式经典用法)

    int32_t  GetPosition() const;       // 普通成员函数(const: 承诺不改对象)
private:
    int32_t  m_position = 0;
    static Encoder s_instance;
};

int32_t Encoder::CountsPerRev() { return (int32_t)PPR * 4; }   // 定义时**不要**再写 static
```

要点清单：

1. **没有 `this` 指针** → 不能访问非静态成员（除非你显式传对象进来）：
   ```cpp
   static int32_t GetPos() { return m_position; }  // ❌ 编译错误: 用了没有 this 的成员
   ```
2. **不能加 `const` / `volatile`**（`const` 修饰的是 `this` 所指对象）；
   **不能是 `virtual`**；不能和同签名的非静态函数共存。
3. 调用方式：`Encoder::CountsPerRev()` —— 不需要对象。
4. 嵌入式里最有用的两个场景：
   - **单例**：`static Encoder& Instance() { static Encoder s; return s; }`
   - **把中断/回调"挂"到类上**（C 风格 API 要求无 this 的普通函数）：
     ```cpp
     extern "C" void TIM3_IRQHandler(void)   // ★ extern "C" 必须: 与启动文件的符号名对接
     {
         Encoder::TIM3_IRQHandler();
     }
     ```
     少了 `extern "C"`，C++ 会把名字修饰成 `_Z...`，链接器按 `TIM3_IRQHandler` 找不到 → **undefined symbol**。

## 6. "只有静态函数能改静态成员"—— 这句话到底是哪来的？

**在 C++ 里也不对，正确的方向是反过来的：**

```cpp
class Encoder {
public:
    void  Set(int v) { s_samples = v; }        // ✅ 普通成员函数改静态成员: 完全合法
    static void SetStatic(int v) { s_samples = v; }  // ✅ 也可以
    static int  Bad(void) { return m_position; }     // ❌ 静态函数访问非静态成员: 不行
private:
    int32_t m_position = 0;
    static  int32_t s_samples;
};
```

| 说法 | 对错 | 正确表述 |
|---|---|---|
| "static 变量只能被 static 函数修改" | ❌ | 任何成员函数（含普通成员函数）都能改静态成员 |
| "static 函数不能改非静态成员" | ✅ | 因为没有 `this`，除非显式传对象进来 |
| "static 表示 private" | ❌ | static 与访问控制无关；私有要写 `private:` |

**对照 Java（我很可能是从这里串过来的）**

| 规则 | Java | C++ |
|---|---|---|
| 静态方法不能直接访问实例成员 | ✅（没有 this） | ✅（同一个道理） |
| 静态成员变量能否被普通（实例）方法修改 | ✅ 能 | ✅ 能 |
| 静态成员变量必须在类外定义 | ❌ 不需要 | ✅ 需要（C++17 之前） |

> 所以记忆口诀：**"静态函数受限制，静态成员不受限制"** —— 方向别记反。

## 7. C++ 里"文件私有"更地道的写法：匿名 namespace

```cpp
// 比 static 更 C++ 风格, 而且能包住类型、模板、常量
namespace
{
    uint32_t s_samples = 0;
    void     Helper() { /* 本文件私有 */ }
    class    Internal { };        // static 做不到这个(static 只能修饰变量/函数)
}
```

在 C++ 里两派都有人用：`static`（与 C 一致，兼容 C 头文件）和匿名 namespace（C++ 味更足）。
**同一个 .c/.cpp 内部，两种方式的效果一样：外部不可见。**

## 8. C++ static 常见错误（面试 & 实战高频）

| 错误 | 编译/链接现象 |
|---|---|
| 声明了类静态成员但忘了类外定义 | `undefined reference to 'Encoder::s_samples'` |
| 静态成员函数里访问非静态成员 | `invalid use of member 'm_position' in static member function` |
| 给静态成员函数加 `const` | `static member function cannot have 'const' qualifier` |
| 写 `virtual static` | `'virtual' can only be used with non-static member functions` |
| 在类内给非 const 静态成员初值（C++11） | `non-const static data member must be initialized out of line` |
| 中断服务函数忘了 `extern "C"` | 链接报 `Undefined symbol TIM3_IRQHandler`（名字被修饰了） |
| 两个静态对象构造互相依赖 | 运行时随机崩溃（静态初始化顺序问题，英文缩写 SIOF） |

## 9. 嵌入式 C++ 落地的实践建议（写给以后的自己）

- **文件私有**：`.cpp` 里用匿名 namespace（或 `static`，跟 C 保持一致也行）；
- **设备实例 / 共享计数**：适合用**类静态成员变量**（例如 `static Encoder s_instance;`）；
- **中断与回调**：用**类静态成员函数** + `extern "C"` 转发（因为 C API 要求无 this 的函数）；
- **单例**：Meyers 写法 `static X& Instance() { static X obj; return obj; }`（C++11 起线程安全）；
- **慎用**"非 POD 的全局/静态对象"和动态内存，嵌入式里能省就省；
- **别把 static 当 private**，也别把它当安全机制 —— 它只是"名字作用域"。

---

## 10. 一页对照总表

| 写法 | 作用域 | 生命周期 | 内存份数 | 典型用途 |
|---|---|---|---|---|
| C：文件级 `static` 变量/函数 | 本文件 | 程序 | 1 | 模块内部状态与私有函数 |
| C：函数内 `static` 变量 | 该函数 | 程序 | 1 | 调用计数、缓存上次值 |
| C++：类 `static` 数据成员 | 类（所有对象共享） | 程序 | 1 | 共享配置、计数器、单例实例 |
| C++：类 `static` 成员函数 | 类（不需要对象） | — | — | 工具函数、单例入口、中断转发 |
| C++：函数内 `static`（局部） | 该函数 | 程序 | 1 | 同上，且 C++11 起初始化线程安全 |
| C++17：`inline static` 成员 | 类 | 程序 | 1 | 省掉类外定义 |
| 普通成员变量 | 对象 | 对象 | 每对象 1 | 每个设备的独立状态 |
| 普通成员函数 | 对象 | — | — | 带 `this` 的操作 |
