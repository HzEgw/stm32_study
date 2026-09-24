# 04 · `pgrep -x <名字>` 查不到进程？—— **`comm` 只保留 15 个字符**

> **记录时间**：2026-09-24
> **触发场景**：想查桥打开了哪个串口，敲了
> `ls -l /proc/$(pgrep -x uart_bridge_node)/fd/ | grep pts` → 报 `ls: 无法访问 '/proc//fd/'`
> **分类判定**：拿掉 ROS2 成立（任何 Linux 进程都一样）→ **工具/环境问题** ✅

---

## 1. 一句话结论

内核里的进程名 **`comm` 上限 15 个字符**（`TASK_COMM_LEN = 16`，含结尾 `\0`）。
`uart_bridge_node` 正好是 **16** 个字符 → 被截断成 **`uart_bridge_nod`** →
**`pgrep -x uart_bridge_node` 匹配不到** → `$(...)` 展开为空 → 命令变成 `ls -l /proc//fd/` → 报"没有那个文件"。

---

## 2. 现象（原样保留）

```
$ ls -l /proc/$(pgrep -x uart_bridge_node)/fd/ | grep pts
ls: 无法访问 '/proc//fd/': 没有那个文件或目录
```
> 👀 **注意路径里的 `/proc//fd/`** —— 两个斜杠中间**是空的**，这就是"变量展开为空"的铁证。

---

## 3. 为什么（实测证据）

```
$ ps -o pid=,comm=,args= -p 15673
   15673 uart_bridge_nod  /home/yin/ros2_ws/install/uart_bridge/lib/uart_bridge/uart_bridge_node --ros-args ...
         ↑↑↑ comm 被截断成 15 字符

$ cat /proc/15673/comm
uart_bridge_nod$                     ← 15 个字符 + NUL

$ pgrep -x uart_bridge_node                     → 0 个   ❌ 你踩的
$ pgrep -x uart_bridge_nod                      → 1 个   ✅
$ pgrep -f 'lib/uart_bridge/uart_bridge_node'   → 1 个   ✅★
```

| 概念 | 是什么 | 长度 |
|---|---|---|
| **`comm`** | 内核记录的**进程名**（`/proc/<pid>/comm`） | **最多 15 字符** |
| **`args`** | **命令行**（`ps -f` 里能看到的那一长串） | 不限 |

→ 所以"`ps` 里名字是全的、`pgrep -x` 却查不到"**并不矛盾**：它们看的不是同一个字段。

---

## 4. 正确写法（抄这三个就够）

```bash
pgrep -a -f uart_bridge                        # ★ 最直观：-a 会把命令行一起打出来，一眼看清匹配到谁
pgrep -f 'lib/uart_bridge/uart_bridge_node'    # 精确（不会误匹配 `ros2 run` 的 python 包装进程）
ps -ef | grep '[u]art_bridge_node'             # 方括号技巧：让 grep 不匹配它自己
```
配合使用（查桥开在哪个串口）：
```bash
ls -l /proc/$(pgrep -f 'lib/uart_bridge/uart_bridge_node')/fd/ | grep pts
```

---

## 5. 正确 ✗ 错误 对照

| ✗ | ✅ |
|---|---|
| 用 `pgrep -x` 查**超过 15 字符**的名字（查不到还没提示） | 用 `pgrep -f`（匹配完整命令行）或 `pgrep -a` 先看它匹配到谁 |
| 以为"`ps` 里是全名，`pgrep` 也该是全名" | `comm`（15 字符）≠ `args`（完整命令行） |
| 变量可能为空还直接拼路径（`/proc/$pid/fd`） | 先 `echo "$pid"` 确认非空 —— **防呆** |

---

## 6. 口诀 & 排查清单

> **口诀：`pgrep -x` 只看 15 字符的 `comm`；名字长过 15 就用 `-f`。看到 `/proc//fd/` 两个斜杠 —— 就是变量空了。**

| # | 症状 | 原因 / 解法 |
|:---:|---|---|
| 1 | `pgrep -x <长名字>` 没输出 | `comm` 截断 → 改 `pgrep -f`（或写截断名） |
| 2 | `/proc//fd/` 报不存在 | 变量展开为空 → 先 `echo "$pid"` |
| 3 | 想知道到底匹配到谁 | `pgrep -a -f <关键字>` |

---

## 7. 关联

| 项 | 内容 |
|---|---|
| 内核常量 | `TASK_COMM_LEN = 16`（含 `\0`）→ 可见名 **15** 字符 |
| 本次用途 | 查 `uart_bridge` 打开了哪个 `/dev/pts/N` → 见本文件夹 **`03`** 篇 |
| 相关命令 | `pgrep -a/-f/-x`、`ps -o comm=,args=`、`cat /proc/<pid>/comm` |

---

## 8. 我的复述（**留空 —— 自己写**）

> 我：__________________________________________________
>
> ______________________________________________________

---

*笔记结束 ｜ 2026-09-24*
