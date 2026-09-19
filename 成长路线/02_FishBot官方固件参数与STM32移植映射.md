# 02 · FishBot 官方固件参数 & STM32 移植映射（权威数据）

> 建档：**2026-09-19**　来源：用户给的门户 **https://fishros.org.cn/**（NodeBB 论坛）+ 官方 GitHub 源码（均已在下方给出可点击链接）
> 用途：本文件是**参数的唯一权威来源**（比商品页截图更硬 —— 直接来自能跑起来的那份固件）。
> ⚠️ 但注意：官方默认值**随批次变化**，最终仍以**你车上实读**为准（见第 7 节）。

---

## 1. 证据链（可点开的原始出处）

| 内容 | 链接 |
|---|---|
| 论坛首页（有 `fishbot` 标签，203 主题） | https://fishros.org.cn/ |
| 论坛 `fishbot` 标签页（**二驱机器人** 板块提问都在这里） | https://fishros.org.cn/forum/tags/fishbot |
| **官方二驱固件源码**（PlatformIO 工程，参数就在里面） | https://github.com/fishros/fishbot_motion_control_microros |
| 固件默认参数头文件 | `include/fishbot_config.h` |
| 运动学实现（正解/逆解/里程计） | `lib/Kinematics/Kinematics.{h,cpp}` |
| 编码器库（PCNT 硬件计数） | https://github.com/fishros/Esp32PcntEncoder |
| 电机库（MCPWM 双 PWM 驱动） | https://github.com/fishros/Esp32McpwmMotor |
| **雷达转接板固件**（ESP8266，串口↔WiFi 透传） | https://github.com/fishros/fishbot-laser-control |
| **官方配套教程《动手学ROS2》第 16 章：FishBot 控制系统搭建** | https://fishros.com/d2lros2/#/humble/chapt16/章节导读 |
| 第 15 章（雷达点云 / 舵机+超声波扫描） | https://fishros.com/d2lros2/#/humble/chapt15/章节导读 |
| 第 13 章（嵌入式基础：GPIO/ADC/I2C/OLED/IMU） | https://fishros.com/d2lros2/#/humble/chapt13/章节导读 |
| 第 14 章（micro-ROS 介绍与安装、话题/服务） | https://fishros.com/d2lros2/#/humble/chapt14/章节导读 |

> **一句话结论**：《动手学ROS2》第 13~16 章就是这台车的"官方实现说明书"（ESP32 版）。
> 你的项目 = **把这 4 章的内容用 STM32F407 重做一遍**，并做对比实验。这条主线现在完全清晰了。

---

## 2. 硬件参数（来自 `include/fishbot_config.h` 的默认值）

### 2.1 运动学 / 控制参数

| 参数 | 宏 / 键名 | 默认值 | 说明 |
|---|---|---|---|
| 减速比 | `MOTOR0/1_PARAM_REDUCATION_RATIO` / `reducate_ration` | **40.5** | 输出轴转一圈，电机转 40.5 圈 |
| 编码器脉冲比 | `MOTOR0/1_PARAM_PULSE_RATION` / `pulse_ration` | **44** | **电机**转一圈产生的脉冲数（= 11 线 × 4 倍频，见 3.1） |
| 轮径 | `MOTOR0/1_PARAM_WHEEL_DIAMETER` / `wheel_diameter` | **65** mm | 与发货清单一致 ✅ |
| **轮距** | `KINEMATIC_WHEEL_DISTANCE` / `wheel_distance` | **172.75** mm | 两轮中心距 —— **odom 必需，之前唯一缺的机械参数** ✅ |
| PID | `MOTOR_PID_KP/KI/KD` | **0.625 / 0.125 / 0.0** | 速度环 |
| PID 输出限幅 | `MOTOR_OUT_LIMIT_LOW/HIGH` | **-100 / 100** | 直接当**占空比百分比**用 |

> 由上面 4 个数即可算出（与 `Kinematics.cpp` 完全一致）：
> ```
> per_pulse_distance = (wheel_diameter × π) / (reducation_ratio × pulse_ration)
>                    = (65 × 3.1415926) / (40.5 × 44) = 204.2035 / 1782 = 0.114592 mm/脉冲
> 每输出轴一圈计数 = 40.5 × 44 = 1782
> ```
> `speed_factor = 1e6 × per_pulse_distance = 114592`（固件里整数化用于 int 运算）

### 2.2 引脚表（ESP32 GPIO → 功能）

| 功能 | GPIO | 备注 |
|---|---|---|
| **电机0** A / B | **22 / 23** | MCPWM unit0 + timer0，双 PWM |
| **电机1** A / B | **12 / 13** | — |
| **编码器0** A / B | **32 / 33** | PCNT unit 0 |
| **编码器1** A / B | **26 / 25** | PCNT unit 1（**注意 A/B 是 26/25 顺序**） |
| IMU（MPU6050） | SDA **18** / SCL **19** | I2C |
| **OLED(0.96")** | SDA **18** / SCL **19** | **与 IMU 同一条 I2C 总线**（地址不同） |
| 电池电压 | ADC **34** | 分压后进 ADC |
| 超声波 | TRIG **27** / ECHO **21** | HC-SR04 |
| 6 路舵机 | 4 / 5 / 14 / 15 / 16 / 17 | 云台/机械臂扩展 |
| 按键 | **0** | OneButton 库 |

### 2.3 电机驱动原理（`Esp32McpwmMotor.cpp`）

- **PWM 频率 5 kHz**；一路 A 给占空比、B 拉低（反转时反之）→ **双 PWM 模式**（不是"PWM + 方向脚"）
- 速度量纲：`-100 ~ +100`（百分比占空比），由 PID 输出限幅决定

### 2.4 编码器计数原理（`Esp32PcntEncoder.cpp`）—— STM32 移植的关键

- PCNT 两个通道分别以 **A 为脉冲、B 为方向** 与 **B 为脉冲、A 为方向** 计数，**双边沿** → 合起来 = **4 倍频**
- 硬件计数器上限 ±100（`EC11_PCNT_DEFAULT_HIGH/LOW_LIMIT`），溢出在中断里累加 `accumu_count` → **32 位累计位置**（软件补位）

> 👉 所以 `pulse_ration = 44 = 11(线) × 4(倍频)`：**车上的霍尔编码器是 11 线**，STM32 用"编码器接口模式"（同样 4 倍频）时，**每电机转一圈的计数同样是 44** —— 参数可以一比一照搬！
> ⚠️ 例外：旧固件示例用的是 **46 / 42**（旧电机），说明这两颗数**必须实车确认**。

### 2.5 固件工程信息（`platformio.ini`，对比实验用得上）

| 项目 | 值 |
|---|---|
| 平台/框架 | PlatformIO + `espressif32` + **Arduino** |
| 板型 | `board = featheresp32`（**普通 ESP32**，不是 S3 —— 以你板上丝印为准） |
| 主频/Flash | **240 MHz** / 80 MHz |
| micro-ROS | `board_microros_distro = humble` + `micro_ros_platformio` |
| 依赖库 | `fishros/Esp32McpwmMotor`、`fishros/Esp32PcntEncoder`、`Adafruit SSD1306`、`OneButton`、`Time`、`fishros/MPU6050_light` |
| 调试串口 | **115200**（`monitor_speed`）；micro-ROS 串口是另一个口（921600） |
| 并行架构 | core0 跑 micro-ROS spin 任务，loop 里跑控制（`xTaskCreatePinnedToCore`） |

> 对比实验的"对手画像"就此明确：**ESP32 @240MHz 双核 Arduino + micro-ROS(WiFi UDP)** vs **STM32F407 @168MHz 单核 FreeRTOS + micro-ROS(串口/Ethernet)**。

---

## 3. ROS2 接口（STM32 版必须**一比一复现**，否则上位机/教程/地图全对不上）

| 项目 | 值 | 来源 |
|---|---|---|
| 节点名 | `fishbot_motion_control` | `CONFIG_DEFAULT_ROS2_NODE_NAME` |
| 命名空间 | 空 | `CONFIG_DEFAULT_ROS2_NAMESPACE` |
| ROS_DOMAIN_ID | 0 | `CONFIG_DEFAULT_ROS2_DOMAIN_ID` |
| **里程计话题（发布）** | **`odom`**（`nav_msgs/msg/Odometry`） | `CONFIG_DEFAULT_ROS2_ODOM_TOPIC_NAME` |
| 发布周期 | **50 ms（20 Hz）** | `CONFIG_DEFAULT_ROS2_ODOM_PUBLISH_PERIOD` |
| odom frame_id | `odom` | `CONFIG_DEFAULT_ROS2_ODOM_FRAME_ID` |
| child_frame_id | `base_footprint` | `CONFIG_DEFAULT_ROS2_ODOM_CHILD_FRAME_ID` |
| **速度指令（订阅）** | **`cmd_vel`**（`geometry_msgs/msg/Twist`） | `CONFIG_DEFAULT_ROS2_CMD_VEL_TOPIC_NAME` |
| IMU（发布） | `imu`（`sensor_msgs/msg/Imu`，仅当 IMU 使能） | `fishbot.cpp` |
| 配置服务 | `fishbot_interfaces/srv/FishBotConfig`（key=value 在线改参数并**存 NVS 掉电保存**） | `fishbot.cpp` |
| micro-ROS 传输 | 默认 **WiFi UDP client**（server `192.168.2.105:8888`）或 **串口**（`serial_id 0`=USB；`2`=GPIO16/17） | `fishbot_config.h` |
| 串口波特率 | **921600**（默认）/ 115200 | `CONFIG_DEFAULT_TRANSPORT_SERIAL_BAUD` |
| 上位机 Agent 命令（UDP） | `docker run -it --rm -v /dev:/dev -v /dev/shm:/dev/shm --privileged --net=host microros/micro-ros-agent:$ROS_DISTRO udp4 --port 8888 -v6` | 官方 README |
| 上位机 Agent 命令（串口） | `... serial --dev /dev/ttyUSB0 -v6 -b 921600` | 官方 README |

**单位约定（很容易搞错）**：`cmd_vel.linear.x` 是 **m/s**，固件内部 `×1000` 变 **mm/s** 再进运动学逆解；`odom.twist.linear.x` 输出前 `/1000` 回到 m/s。

---

## 4. 运动学与里程计（`Kinematics.cpp` 官方实现，可直接抄进 STM32）

```cpp
// 逆解：线速度(mm/s) + 角速度(rad/s) -> 左右轮线速度(mm/s)
v_L = linear_speed - (angular_speed * wheel_distance) / 2.0;
v_R = linear_speed + (angular_speed * wheel_distance) / 2.0;

// 正解：左右轮线速度 -> 线速度(mm/s) + 角速度(rad/s)
linear_speed  = (v_L + v_R) / 2.0;
angular_speed = (v_R - v_L) / wheel_distance;

// 轮速换算（每周期调用；dt 单位 us）
motor_speed = dtick * (speed_factor / dt)          // = Δcount × 1e6 × per_pulse_distance / dt

// 里程计积分
yaw += angular_speed * dt_s;  TransAngleInPI(yaw);   // 归一化到 (-π, π]
x   += (linear_speed/1000) * dt_s * cos(yaw);
y   += (linear_speed/1000) * dt_s * sin(yaw);
// 姿态四元数：Euler2Quaternion(0, 0, yaw, q)
```

> 注意官方代码里有个**已知小坑**：`TransAngleInPI()` 写的是 `out_angle -= 2π`（用的是**入参**而不是 `out_angle`），归零时角度可能一直停在边界 —— 你自己实现时写成 `angle -= 2π` 更正确（**这正好是报告里"发现并修复官方固件的缺陷"的素材**）。

---

## 5. 雷达链路（重要认知修正）

**转接板不解析协议，只是"UART → WiFi 透传"**（`fishbot-laser-control`，**ESP8266** 固件）：

| 配置项 | 说明 |
|---|---|
| `laser_baud` | **雷达串口波特率（可配！）** |
| `motor_speed` | 雷达转台电机转速（决定扫描频率） |
| `net_mode` | `udp` / `tcp`（默认 tcp），端口如 `tcp:8889` |
| `server_ip` / `server_port` | 上位机地址 |

含义：
1. **协议解析在上位机**（ROS2 驱动包里把原始帧解成 `/scan`）→ 所以"雷达型号/协议"只影响 **STM32 直连**这条路，不影响现有工作流；
2. 仓库 `test/` 下有历史测试程序 **`ydlidar_x2_ros2`**（1 MB 二进制），提示车队里出现过 **YDLIDAR X2 系**雷达 —— 但这**不是**你那台"MiniV1"的定论，**仍要拍标签确认**；
3. STM32 直连雷达需要三件事：**波特率**（= 转接板 `laser_baud`，读配置可知）、**帧协议**（ROS2 驱动源码）、**转台电机供电/转速控制**（`motor_speed`）。

---

## 6. 学习工程 ↔ 官方教程章节对照（W2~W6 的路线图）

| 我们的工程（F103） | 官方教程章节（ESP32） | 结论 |
|---|---|---|
| `01_PWM_Output(_Reg)` | 16.2 从H桥说起 / 16.3 正反转 / 16.4 速度控制 | 已覆盖 ✅ |
| `02_Input_Capture(_Reg)` | 15.2 超声波测距（echo 脉宽） | 已覆盖，且可用车上 SR04 实战 ✅ |
| `03_Encoder(_Reg)` | 16.7 编码器测速 / 16.8 脉冲测量与校准 | 已覆盖，参数用 §2.1 ✅ |
| — | 16.10 PID 速度环 | **W3~W4 做**（C 语言 PID + 限幅 ±100） |
| — | 16.11~16.14 运动学正逆解 + 里程计 | **W4~W5 做**（照 §4 公式移植） |
| `04_UART_Bluetooth(_Reg)` | 16.6 订阅 Twist 遥控车（学习用桥） | 已覆盖 ✅ |
| `05_I2C_MPU6050` | 13.2.2 IMU / 13.2.6 I2C 点亮 OLED | 已覆盖，可再加 OLED 挂同总线 ✅ |
| — | 16.5 多路电机库 / 16.9 最大速度测量 | W4 做（最大速度 = 对比实验数据） |
| —（F407 阶段） | 14.x micro-ROS 话题/服务 + 15.x 雷达点云 | **W8~W12 主线** |

---

## 7. STM32F407 移植映射表（ESP32 → STM32）

| 功能 | ESP32 官方方案 | STM32F407 方案 | 学习/对比价值 |
|---|---|---|---|
| 电机驱动 | MCPWM **双 PWM，5 kHz**，速度 ±100(%) | TIM1/TIM8 两通道 PWM（5 kHz 或 20 kHz）；**可用"互补输出 + 死区"** | 死区/互补是 F1 学习工程 `01` 的进阶点 |
| 编码器 | PCNT，**4 倍频**，±100 硬上限 + 软件累加 | TIM 编码器接口模式（**同样 4 倍频**，参数直接照搬 44） | **F407 的 TIM2/TIM5 是 32 位**，天然无溢出 → 比官方方案更简洁（可写进报告） |
| IMU | MPU6050_light（Wire，GPIO18/19） | I2C1（PB6/PB7）+ 自己的驱动（`05_I2C_MPU6050` 已有） | 已具备 |
| OLED | Adafruit SSD1306（同 I2C 总线） | I2C1 共挂（0x3C），可复用现有 I2C 驱动 | I2C 多设备练习 |
| 电池电压 | ADC GPIO34 | ADC1 + 分压 | 新知识点（ADC） |
| 超声波 | GPIO + GPIO 中断计时 | TRIG 定时器输出 + **ECHO 输入捕获**（`02_Input_Capture`） | ⚠️ ECHO 5V 必须分压 |
| ROS2 桥 | micro-ROS + **WiFi UDP**（8888）/ 串口 921600 | micro-ROS + **串口 transport**（USB-TTL，921600 或 115200）；板上带网口可换 Ethernet | **WiFi UDP vs 串口/Ethernet 的延迟对比 = 天然实验数据** |
| 参数存储 | NVS（Preferences） | STM32 内部 Flash 或 W25Qxx（后续做） | 可后置 |
| 实时性 | Arduino loop + 双核任务（core0 跑 micro-ROS） | **FreeRTOS**：控制任务 + 通信任务 + 里程计任务 | 这正是"自研固件"的卖点 |

**必须保持一致（否则上位机侧全乱）**：节点名 `fishbot_motion_control`、话题 `odom`(20Hz) / `cmd_vel` / `imu`、`frame_id=odom`、`child_frame_id=base_footprint`、`linear.x` 单位 m/s。

---

## 8. 实车确认清单（今天就能做，30 分钟）—— 把这些数填回本文件

> 最省事的办法：**先跑官方固件**，用 ROS2 读参数（比翻源码快）。

```bash
# 1) 看服务与话题（确认节点名/话题名/frame）
ros2 node list
ros2 topic list | grep -E "odom|cmd_vel|imu|scan"
ros2 topic info /odom
ros2 topic hz /odom            # 期望 ~20Hz

# 2) 读官方固件里的"真实参数"（关键一步！）
ros2 service list | grep -i config
ros2 service call /fishbot_motion_control/fishbot_config fishbot_interfaces/srv/FishBotConfig \
  "{key: 'command', value: 'read_config'}"     # 具体服务名以 ros2 service list 为准

# 3) 看里程计输出单位与数值
ros2 topic echo --once /odom
```

| 要确认的数 | 官方默认（本文件 §2.1） | 你的车实测 | 怎么测 |
|---|---|---|---|
| 减速比 | 40.5 | ______ | 读配置服务；或 16.8 脉冲测量与校准 |
| 编码器脉冲比 | 44（=11 线×4） | ______ | 同上；或手转轮一圈数 `03_Encoder` 计数 |
| 轮径 | 65 mm | ______ | 卡尺（含胎） |
| **轮距** | **172.75 mm** | ______ | 卷尺（两轮接地面中心距） |
| PID | 0.625 / 0.125 / 0.0 | ______ | 读配置服务 |
| 最大线速度 | 16.9 章节实测 | ______ | 遥控拉满 + `ros2 topic echo /odom` 看 `linear.x` |
| 雷达波特率 | 转接板 `laser_baud` | ______ | 串口发 `$command=read_config` 给转接板 |
| 雷达型号 | 待拍标签 | ______ | 拍照（本文件 §5 说明为什么重要） |

> ⚠️ 读到 40.5/44 还是 46/42，取决于**你的电机批次** —— 这就是为什么必须实读，不能照抄。

---

## 9. 对比实验素材（直接可写进挑战杯报告）

| 对比维度 | ESP32-S3 官方固件 | STM32F407 自研固件 | 怎么测 |
|---|---|---|---|
| 编码器计数位宽 | PCNT 8 位硬上限 ±100 + 软件累加 | TIM2/TIM5 **32 位原生** | 代码分析 + 长时间累计漂移对比 |
| 电机 PWM 能力 | MCPWM 5 kHz 双 PWM | 高级定时器 + **死区/互补**、更高频率 | 示波器波形 |
| 通信链路 | WiFi UDP（端口 8888） | 串口 921600 / Ethernet | `ros2 topic delay` / 时间戳差 |
| 任务架构 | Arduino loop + 双核 | **FreeRTOS 多任务 + 优先级** | 控制周期抖动（示波器 + 时间戳） |
| 参数存储 | NVS | Flash 模拟 EEPROM | 掉电恢复测试 |
| 资源占用 | `platformio run` 的 flash/RAM 报告 | Keil 的 Code/RW/ZI Data | 表格对比 |

---

## 10. 下一步（W1 收尾 → W2）

1. **读一遍官方教程第 16 章**（ESP32 版），把 16.7~16.14 的**思路**吃下来（实现要用 STM32 重写）；
2. 按 §8 用官方固件把参数读全 → 回填本文件 + `01_FishBot硬件清点表.md`；
3. 上电前先做"三测"：编码器供电电压、6P 线序、ECHO 电平分压（安全第一）；
4. **准备工作已完成**：`05_I2C_MPU6050_Reg`（I2C 寄存器版）与 `06_Kit_Check`（套件验收）两个工程**均已写好并登记进工作区** —— 到 W3 直接打开 Keil 建工程即可；
5. **时间纪律**：W1 只做 git + 仓库；读参数/量电压/接线等"碰实物"的动作，安排在 W1 周末（可选）与 **W3 接线之前**（见 `成长路线\README.md` 的"时间纪律"表）。

