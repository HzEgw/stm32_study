# 02 · 怎么查看 ROS2 / C++ 库的源码（VS Code + 命令行 + GitHub）

> **建立时间**：2026-09-20
> **起因**：我说过一句话——"**因为 C++ 我看不到具体的源码，所以时常有很多困惑**"。
> 结果发现：**源码一直就在我自己的硬盘里**（`/opt/ros/humble/include/...`），只是我不知道去哪看、怎么看。
> 这一篇把"看源码"这件事变成肌肉记忆，以后不再靠猜。

---

## 0. 先破除一个误解：ROS2 的 C++ 源码**大部分你能看到 100%**

| 代码类型 | 在哪 | 能看到吗 |
|---|---|---|
| **模板**（`create_publisher`、`Publisher<T>`、`rclcpp::Node` 的大部分） | **头文件** `.hpp` | ✅ **全部可见**（模板必须写在头文件里） |
| 非模板实现（`node_topics.cpp`、`rclcpp` 的 .cpp） | 编译进 `.so` 库 | 🟡 本机只有二进制 → 去 GitHub 看 |
| 底层 C 库（`rcutils`、`rmw`、DDS） | 头文件 + `.so` | ✅ 头文件可见；实现去 GitHub |

> 关键点：**ROS2 的 C++ API 是"重头文件"的**（模板 + 内联），
> 所以你日常 90% 的困惑（"这个函数到底返回什么/怎么实现的"）**在本机就能解决**。

---

## 1. 三条路，按场景选

### 路线 A：VS Code 里"点一下"（**日常最常用**）

| 操作 | 快捷键 | 用途 |
|---|---|---|
| **跳转到定义** | **`F12`**（或 `Ctrl + 点击`） | 光标放在 `create_publisher` 上按 F12 → 直接跳到 `node.hpp` |
| 快速打开文件 | `Ctrl + P` → 输入 `node.hpp` | 知道文件名时最快 |
| **全仓搜索** | **`Ctrl + Shift + F`**（可指定目录） | 搜"关键字"，如 `add_publisher` |
| 查看引用/被谁调用 | `Shift + F12` | 想知道"谁调用了这个函数" |
| 返回上一处 | `Alt + ←` | 跳过去再跳回来 |

⚠️ **前提**：VS Code 的 IntelliSense 要能找到这些头文件。
判据看你的工作区 `.vscode/c_cpp_properties.json` 里有没有：
```json
"includePath": [
  "/opt/ros/humble/include/**",
  "${workspaceFolder}/src/**",
  "/usr/include/**"
]
```
（`~/ros2_study_myself/w1_study_workspace/.vscode/c_cpp_properties.json` **已经配好**；
换新工作区时把这段复制过去即可。）

### 路线 B：命令行 grep / sed（**最快、最可靠、不依赖 IDE**）

```bash
# ① 在整棵 ROS2 头文件树里搜关键字（带行号）
grep -rn "create_publisher" /opt/ros/humble/include/rclcpp/ | head

# ② 只看头文件（避免噪音）
grep -rn --include=*.hpp "add_publisher" /opt/ros/humble/include/rclcpp/

# ③ 拿到行号后，看上下文（读第 40~100 行）
sed -n '40,100p' /opt/ros/humble/include/rclcpp/rclcpp/create_publisher.hpp

# ④ 找"某个类都有哪些成员"（看 private 段）
grep -n "private:" -A 15 /opt/ros/humble/include/rclcpp/rclcpp/node_interfaces/node_topics.hpp

# ⑤ 找"这个宏/类型到底是什么"
grep -rn "define RCLCPP_SMART_PTR_DEFINITIONS" /opt/ros/humble/include/rclcpp/
```

### 路线 C：GitHub / 在线（**看 `.cpp` 实现和版本差异**）

| 想看什么 | 去哪 |
|---|---|
| rclcpp 的实现（`.cpp`） | `https://github.com/ros2/rclcpp` → **切 `humble` 分支** |
| rcl / rmw / rcutils | `github.com/ros2/rcl`、`.../rmw`、`.../rcutils` |
| API 文档（有搜索） | `docs.ros.org/en/humble/` |
| **C++ 语言本身**（`shared_ptr`、`vector`…） | `cppreference.com`（中文：`zh.cppreference.com`） |
| 本机已装包的源码（可选） | `apt-get source ros-humble-rclcpp`（需先加 `deb-src` 源，见下） |

```bash
# 想用 apt-get source 的话（Ubuntu 22.04，先确认有 deb-src 源）
grep -rn "^deb-src" /etc/apt/sources.list /etc/apt/sources.list.d/ 2>/dev/null
# 没有就加（以清华源为例）：
# deb-src https://mirrors.tuna.tsinghua.edu.cn/ubuntu/ jammy main universe multiverse restricted
sudo apt update && apt-get source ros-humble-rclcpp
```

---

## 2. 实战：定位我贴过的那段 `create_publisher`（完整链路）

```
① 声明处（我贴的那段）：  node.hpp:195          ← 光标按 F12 就会到这
② 转发到三个重载：        create_publisher.hpp:94 / 113
③ 真正干活的 detail 版：  create_publisher.hpp:46
      ├─ detail::create_publisher(...)
      │    auto pub = node_topics_interface->create_publisher(...)   ← 建对象
      │    node_topics_interface->add_publisher(pub, ...)            ← 只是“登记”
      │    return std::dynamic_pointer_cast<PublisherT>(pub);        ← 所有权交给你
      └─ 真正 new 的地方：  publisher_factory.hpp:76   auto publisher = std::make_shared<PublisherT>(...)
④ 节点侧到底存不存它？    node_topics.hpp:92-96   private 成员只有 node_base_ / node_timers_
⑤ 回调组里存什么？        callback_group.hpp:226-230  全是 WeakPtr（连 publisher 的容器都没有）
```

**这条链就是"为什么 `pub_` 必须自己存"的源码级证据**（对应笔记 `学习C_C++语言补充\07`、`10`）。

---

## 3. 报错时怎么用（把报错直接变成"源码坐标"）

编译器的报错格式就是 `文件:行:列: error/warning: 说明`，**拿这个路径直接打开**：

```
w1_subscriber_demo.cpp:12:40: warning: format '%s' expects argument of type 'char*',
  but argument 5 has type 'const std::string' [-Wformat=]
      note: in definition of macro 'RCUTILS_LOG_COND_NAMED'
   79 |       rcutils_log(&__rcutils_logging_location, severity, name, __VA_ARGS__);
```
→ 顺藤摸瓜：`F12` 点 `RCLCPP_INFO` → 看它展开成什么 → 搜 `rcutils_log` 的签名。
**报错里的每一层 `note:` 都是一级"源码跳转"**。

---

## 4. 命令速查卡

| 目标 | 命令 |
|---|---|
| 搜关键字（全树，带行号） | `grep -rn "关键字" /opt/ros/humble/include/` |
| 只看头文件 | `grep -rn --include=*.hpp "关键字" 目录` |
| 看某段代码 | `sed -n '起,止p' 文件` |
| 找类的成员 | `grep -n "private:" -A 15 文件` |
| 找宏定义 | `grep -rn "define 宏名" /opt/ros/humble/include/` |
| 找某个包装在哪 | `ros2 pkg prefix rclcpp` |
| 找某个头文件 | `ls /opt/ros/humble/include/rclcpp/rclcpp/ \| grep 关键字` |
| 看包里的可执行文件 | `ros2 pkg executables <包名>` |

---

## 5. 关联

| 项 | 内容 |
|---|---|
| 本次实战对象 | `create_publisher` 全链路（见 §2）；结论已写进 `..\学习C_C++语言补充\07`、`..\10` |
| 我的工作区配置 | `~/ros2_study_myself/w1_study_workspace/.vscode/c_cpp_properties.json`（已含 `/opt/ros/humble/include/**`） |
| 相关笔记 | `01_git速查与工作流.md`（另一个"零基础→够用"的工具篇） |
| ROS2 侧笔记 | `~/ros2_study_myself/学习笔记/`（`01`、`02` 篇） |

---

*笔记结束 ｜ 2026-09-20*
