# uart_bridge —— STM32 串口帧协议 ↔ ROS2 桥（C++ / rclcpp）

> 这是 **W2 的收尾工程**：把 `04_UART_Bluetooth` 发出的帧变成 ROS2 话题。
> 它是 **micro-ROS 的"前身"**：先手写一遍桥（理解每个字节怎么走），再用 micro-ROS 替代它，
> 你就彻底明白"上/下位机之间的桥"到底在做什么。

---

## 0. 重要：源码在 Windows，编译在 Linux

| 事项 | 说明 |
|---|---|
| 源码位置 | `D:\STM32F103C8_Workspace\ros2_ws_src\uart_bridge`（随工作区一起进 git） |
| 编译位置 | **必须在 Linux ext4 上**（`~/ros2_ws`）；NTFS 上 `colcon build` 会出权限/大小写问题 |
| 同步方式 | `git`：Windows 提交推送 → Linux `git pull` |

Linux 端一次性操作：
```bash
mkdir -p ~/ros2_ws/src && cd ~/ros2_ws/src
# 方式 A: 从 git 克隆整个工作区仓库
git clone <你的仓库地址> workspace_repo
cp -r workspace_repo/ros2_ws_src/uart_bridge ~/ros2_ws/src/
# 方式 B: 已经把 uart_bridge 提交到独立仓库时
# git clone <uart_bridge 仓库地址>

cd ~/ros2_ws
colcon build --packages-select uart_bridge --symlink-install
source install/setup.bash
```

## 1. 运行

```bash
# 1) 先看串口设备名（插上 USB-TTL 后）
ls -l /dev/ttyUSB* /dev/ttyACM*

# 2) 权限（只需一次）：把自己加进 dialout 组, 重新登录生效
sudo usermod -aG dialout $USER

# 3) 启动桥
ros2 launch uart_bridge uart_bridge.launch.py port:=/dev/ttyUSB0 baud:=115200
# 或 ros2 run uart_bridge uart_bridge_node --ros-args -p port:=/dev/ttyUSB0
```

## 2. 验证（三个终端）

```bash
# 终端 A：看原始帧（payload 字节数组）
ros2 topic echo /mcu/frame

# 终端 B：看 MCU 计数器（payload 前 2 字节，大端）—— 应该每 100ms +1
ros2 topic echo /mcu/counter

# 终端 C：给 MCU 发一帧（会被 STM32 原样回显, 再从 /mcu/frame 看到）
ros2 topic pub --once /mcu/tx std_msgs/msg/UInt8MultiArray "{data: [0x11, 0x22]}"

# 看频率 与 话题信息
ros2 topic hz /mcu/counter
ros2 topic info /mcu/frame -v
```

**验收标准（W2 的目标）**：
- `ros2 topic echo /mcu/counter` 每 100ms 打印一次递增的数值 ✔
- `ros2 topic pub /mcu/tx ...` 后能在 `/mcu/frame` 看到回显 ✔

### 2.1 没有硬件也能测：假数据合流（`socat`，2026-09-23 补）

**不需要 STM32、不需要 USB-TTL**：在本机造一对**互相对通的虚拟串口**，一头给桥、一头自己灌字节：

```bash
sudo apt install -y socat
socat -d -d pty,raw,echo=0 pty,raw,echo=0     # 终端 A：记下打印的两个 /dev/pts/N（保持不关）
```
```bash
# 终端 B（先 socat 后起桥）：让桥去开"另一头"
ros2 run uart_bridge uart_bridge_node --ros-args -p port:=/dev/pts/6
# 终端 C：ros2 topic echo /mcu/frame
# 终端 D：往"你这一头"灌一帧 —— AA 55 | 02 | 00 07 | 09
#         (LEN=2，SUM=(2+0+7)&0xFF=0x09)
printf '\xAA\x55\x02\x00\x07\x09' > /dev/pts/5
```
→ `/mcu/frame` 出 `data: [0, 7]`、`/mcu/counter` 出 `data: 7` = **桥通**；
→ 故意写错 SUM（`...\x08`）应看到**话题不动 + 日志"校验错"+1**。

> **用途**：W2 还没接 MCU 时，先证明"**协议 + 桥**"这一段是对的 —— 这样 W6 接上真硬件时，出问题必定在**硬件/线路**，而不在协议。
> ⚠️ `socat` 那个终端**不能关**（关了对端设备消失）；`/dev/pts/N` 每次重建都会变。
> 双系统（Windows Keil + Ubuntu ROS2）下的串口归属与切换流程见 `成长路线\04_Ubuntu侧开工清单.md` **§3.1**。

## 3. 协议（与 STM32 侧完全一致）

```
0xAA 0x55 | LEN(1B) | PAYLOAD[LEN] | SUM(1B)
SUM = (LEN + ΣPAYLOAD) & 0xFF        LEN ∈ [1, 32]
```
| 话题 | 类型 | 方向 | 说明 |
|---|---|---|---|
| `/mcu/frame` | `std_msgs/UInt8MultiArray` | MCU → ROS2 | 原始 payload |
| `/mcu/counter` | `std_msgs/UInt16` | MCU → ROS2 | payload 前两字节（大端）当计数器 |
| `/mcu/tx` | `std_msgs/UInt8MultiArray` | ROS2 → MCU | 自动打包成帧发出 |

## 4. 参数

| 参数 | 默认 | 说明 |
|---|---|---|
| `port` | `/dev/ttyUSB0` | 串口设备（USB-TTL）/ `/dev/ttyACM0`（部分模块） |
| `baud` | `115200` | 必须与 MCU 一致 |
| `publish_counter` | `true` | 是否额外发布 `/mcu/counter` |

## 5. 常见问题

| 现象 | 原因/解决 |
|---|---|
| `打开串口失败 ... Permission denied` | 没加 `dialout` 组，或没用 `sudo`（加组后要重新登录） |
| `打开串口失败 ... No such file` | 设备名不对：`ls /dev/ttyUSB* /dev/ttyACM*` 确认；换 USB 口后编号会变 |
| 收到的帧 `data` 长度乱、校验错多 | 波特率不一致 / 未共地 / 线太长 |
| `ros2 topic echo /mcu/counter` 没数据 | MCU 没在发（先确认串口助手能收到帧）；或 `publish_counter:=false` |
| 每 100ms 一帧但数值不增长 | MCU 那边计数器没自增（看 `04_*` 的 main.c） |
| `colcon build` 报找不到 rclcpp | 没 `source /opt/ros/humble/setup.bash` |

## 6. 下一步（W2 之后）

1. 把 `/mcu/frame` 里的数据**解析成有意义的量**（电压/转速/姿态角），发布成正式话题；
2. **用 micro-ROS 替换这个桥**（W6~W7）：MCU 自己就是 ROS2 节点，这个 `uart_bridge` 就不再需要了 —— 那时你会真正理解"桥"与"原生节点"的区别；
3. 把这条链路接到 FishBot 上：STM32 读编码器/IMU → 帧 → ROS2 → 建图/导航。
