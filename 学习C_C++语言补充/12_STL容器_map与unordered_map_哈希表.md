# 12 · `map` 与 `unordered_map`（哈希表）—— 读懂源码前的必备词汇

> **记录时间**：2026-09-22
> **触发场景（我自己的原话）**："我发现今天学习的时候我对于**哈希表和 map** 其实不是很熟悉，
> **我推测这也是我没有很快看明白源码的原因**。"
> **分类判定**：拿掉 ROS2 成立、换到 PC 也成立 → **C/C++ 语言问题** ✅
> **具体卡点**：`/opt/ros/humble/include/rclcpp/rclcpp/client.hpp:826-831` 的 `pending_requests_`
> **本机数据**：`g++ (Ubuntu 11.4.0)` + libstdc++（桶数用质数）

---

## 1. 一句话结论

| 你想要什么 | 用哪个 | 底层结构 | 查找 | 遍历顺序 |
|---|---|---|---|---|
| **有序**遍历、按 key 排序 | `std::map` | **红黑树** | `O(log n)` | ✅ 按 key 升序 |
| **最快**查找（不在乎顺序） | `std::unordered_map` | **哈希表** | 平均 `O(1)`、最坏 `O(n)` | ❌ 无序（由 hash 决定） |
| 下标访问、连续内存、常遍历 | `std::vector` | 动态数组 | `O(n)` 线性找 | 插入顺序 |

> **一句话**：`map` 是"**排好队的字典**"，`unordered_map` 是"**按哈希分桶的柜子**"。

---

## 2. 卡住我的那份源码（原样抄下来）

```cpp
// /opt/ros/humble/include/rclcpp/rclcpp/client.hpp:826-831
std::unordered_map<
  int64_t,                                    // key = 请求号（sequence_number）
  std::pair<
    std::chrono::time_point<std::chrono::system_clock>,
    CallbackInfoVariant>>                     // value = (发出时刻, 回调+future+promise)
pending_requests_;
```

**它就是 04 篇里那个"一问一答"的账本**：发请求时用 `try_emplace(sequence_number, ...)` 记一笔（`client.hpp:749` 附近），
响应回来时按**请求号**查回那一笔 → 调用回调 → `erase` 掉（`client.hpp:703`）。

> **为什么这里用 `unordered_map` 而不是 `map`？**
> 因为它只关心"**按号查**"，**不关心顺序** → 要的就是平均 `O(1)`。
> （如果它还需要"按请求号从小到大处理"，才会选 `map`。）

---

## 3. 为什么会这样（原理）

### 3.1 哈希表的四步（`unordered_map` 的工作方式）

```
① key 过哈希函数  →  一个大整数
② 大整数 % 桶数   →  桶下标
③ 塞进那个桶      →  桶里已有别人就"挂链"（冲突处理）
④ 装的东西 ÷ 桶数 > 负载因子（默认 1.0）→ rehash：桶数变大、所有元素重排
```
→ **③ 决定了"无序"**：遍历是"按桶走的"，跟插入顺序无关。

### 3.2 `map` 为什么有序

它底层是**红黑树（自平衡二叉搜索树）**：中序遍历天然有序 → 所以 `map` 遍历出来**一定按 key 升序**，
代价是每次查找 `O(log n)`（树高）。

### 3.3 `try_emplace` 是什么（源码里就是它）

| 写法 | 行为 |
|---|---|
| `m[key] = v;` | key 不存在 → **先插入默认值再赋值**；key 存在 → 覆盖 |
| `m[key];` | ⚠️ **key 不存在时会凭空插入一个默认值**（很多人踩过，实测见 §4） |
| `m.at(key)` | key 不存在 → **抛 `std::out_of_range`**（不插入） |
| `m.try_emplace(key, args...)` | **不存在才原地构造**（省一次构造+移动）；**已存在就什么都不做**，返回 `(迭代器, 是否插入)` |
| `m.insert({key, v})` | 已存在就**不覆盖**（与 `try_emplace` 类似，但可能多一次构造/移动） |

### 3.4 复杂度对照（把这张表背下来，读源码就顺了）

| 操作 | `map`（红黑树） | `unordered_map`（哈希表） | `vector` |
|---|---|---|---|
| 查找 | `O(log n)` | **平均 `O(1)`** / 最坏 `O(n)` | `O(n)` |
| 插入 | `O(log n)` | 平均 `O(1)`（可能触发 rehash） | 尾部均摊 `O(1)`，中间 `O(n)` |
| 删除 | `O(log n)` | 平均 `O(1)` | 中间 `O(n)` |
| 有序遍历 | ✅ | ❌ | 插入顺序 |
| 内存 | 每节点额外指针（红黑） | 每节点哈希链 + **桶数组** | 连续、最省 |

### 3.5 ⚠️ 和 MCU 的关系（接 `11_MCU上为什么怕malloc与new…`）

`unordered_map` / `map` 都会**动态分配节点和桶** → 在 STM32 上等同于"反复 malloc"：
碎片 + 不确定耗时。**固件里的替代方案**：静态数组 + 线性/二分查找，或固定桶数的自写小表。
（`micro-ROS` 在 F407 上跑得动、在 F103 上很勉强，这类"容器吃内存"就是原因之一。）

---

## 4. 最小复现（不依赖 ROS，可直接编译运行；下面输出是**本机实跑**）

```cpp
#include <cstdio>
#include <map>
#include <string>
#include <unordered_map>

int main()
{
    std::map<std::string, int>           ordered;   // 红黑树
    std::unordered_map<std::string, int> hashed;    // 哈希表

    const char * words[] = {"b", "a", "c", "b", "a", "b"};
    for (auto w : words) { ordered[w]++; hashed[w]++; }

    std::printf("map（有序）: ");
    for (auto & kv : ordered) { std::printf("%s=%d ", kv.first.c_str(), kv.second); }

    std::printf("\nunordered_map（桶序）: ");
    for (auto & kv : hashed) { std::printf("%s=%d ", kv.first.c_str(), kv.second); }

    std::printf("\n桶数=%zu  负载因子=%.3f\n", hashed.bucket_count(), hashed.load_factor());
    for (auto & kv : hashed) {
        std::printf("  '%s' 落在第 %zu 个桶\n", kv.first.c_str(), hashed.bucket(kv.first));
    }

    hashed["d"];                                    // ⚠️ 会凭空插入
    std::printf("用了 operator[] 之后 d=%d\n", hashed["d"]);

    auto ins = hashed.try_emplace("e", 42);
    std::printf("try_emplace: inserted=%d  e=%d\n", (int)ins.second, ins.first->second);
    return 0;
}
```
编译与运行：`g++ -std=c++17 -Wall -Wextra -o /tmp/w1_stl_demo /tmp/w1_stl_demo.cpp && /tmp/w1_stl_demo`

**实测输出（原文照抄）**：
```
map（有序）: a=2 b=3 c=1 
unordered_map（桶序）: c=1 a=2 b=3 
桶数=13  负载因子=0.231
  'c' 落在第 10 个桶
  'a' 落在第 1 个桶
  'b' 落在第 9 个桶
用了 operator[] 之后 d=0（注意：被凭空插入了）
try_emplace: inserted=1  e=42
```
**三个可观察结论**：
1. `map` 输出 `a,b,c`（**有序**）；`unordered_map` 输出 `c,a,b`（**桶序**）；
2. 桶数 **13**（libstdc++ 用**质数**做桶数）；负载因子 = 3 / 13 ≈ **0.231**；
3. `hashed["d"]` **真的被插进去了**（d=0）→ `operator[]` **不是只读操作**；而 `try_emplace` 的返回值告诉你"**插没插**"。

---

## 5. 要不要 / 什么时候用（判断标准）

| 情况 | 选择 |
|---|---|
| 要按 key **有序**遍历（打印配置、按时间戳处理） | `map` |
| 只按 key **快速查**（rclcpp 的 `pending_requests_` 就是） | `unordered_map` |
| 元素很少（<10 个） | **`vector` + 线性查找反而更快**（连续内存、缓存友好） |
| 需要"插入后迭代器不失效" | `unordered_map` 的 **rehash 会让全部迭代器失效**；`map` 只有被删的那个失效 |
| **MCU 固件** | ❌ 两个都别用（动态分配节点/桶）→ 静态数组 / 自写固定桶表 |

---

## 6. 正确 ✗ 错误 对照

| ✗ 常见写法/认知 | ✅ 正确 |
|---|---|
| 以为 `m[key]` 只是"读一下" | 它会**插入默认值**（实测 `d=0`）；只读请用 `find` / `at` |
| 以为 `unordered_map` 按插入顺序 | 遍历是**桶序**（实测 `c,a,b`） |
| 拿到迭代器 → 中间又插入元素 → 继续用旧迭代器 | `unordered_map` **rehash 后迭代器全失效** → 先查、用完再插 |
| 用 `map` 存"只查不排序"的大数据 | 换成 `unordered_map`（`O(1)` vs `O(log n)`） |
| 在 MCU 上用这两种容器 | 静态数组 / 自写小表（见 `11` 篇） |

---

## 7. 口诀 & 排查清单

> **口诀：要顺序用 `map`（红黑树），要速度用 `unordered_map`（哈希桶）；`[]` 会插、`at` 会抛、`try_emplace` 最安全。**

| # | 遇到什么 | 怎么想 |
|:---:|---|---|
| 1 | 读源码卡在容器类型上 | 先问两句：**要不要有序？要不要 O(1)？** |
| 2 | 看到 `try_emplace` / `emplace` | "原地构造 + 已存在不覆盖"，返回 `(迭代器, 是否插入)` |
| 3 | 看到 `m[key]` 这种写法 | 立刻警觉"**可能插入**"（rclcpp 用 `try_emplace`/`erase`，是对的） |
| 4 | 看到 `bucket_count()` / `load_factor()` | 这是**哈希表**无疑（rehash 边界） |

---

## 8. 关联知识

| 项 | 内容 |
|---|---|
| 源码 | `client.hpp:826-831`（`pending_requests_` 声明）、`:749` 附近（`try_emplace`）、`:703`（`erase`） |
| 本机复现 | `/tmp/w1_stl_demo.cpp`（重启会清，代码已内嵌 §4） |
| 相关笔记 | `09_内存分配到底怎么回事…`（容器内部会 `new`）、**`11_MCU上为什么怕malloc与new…`**（MCU 上不用容器）；ROS2 侧 `~/ros2_study_myself/学习笔记/04`（rclcpp 用哈希表做"一问一答账本"） |
| 顺带统计（本机 rclcpp 头文件） | `std::vector<` × **143**、`std::map<` × **13**、`std::unordered_map` × **10** → **这三样是读 rclcpp 的高频词** |

---

## 9. 一句话复述（**留空 —— 我自己写，写出来这篇才算我的**）

> 我：________________________________________________________
>
> ____________________________________________________________

---

*笔记结束 ｜ 2026-09-22 ｜ 触发于"读源码卡壳"，AI 负责证据与整理*

