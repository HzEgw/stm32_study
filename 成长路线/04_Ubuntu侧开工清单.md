# 04 · Ubuntu 侧开工清单（ROS2 + git，**全程不需要代理**）

> **用途**：W1 周日（09-20）R 线的操作手册 —— 照着敲即可，不用做决策。
> **前提**：Ubuntu 22.04 双系统 + ROS2 Humble 已装（若没装，见 §2.4）。
> **为什么不用代理**：git 走 **SSH**（22/443 端口）+ 依赖走**国内镜像源** —— 既快，也不涉及校园网风险。

---

## 0. 两边的分工（先记住这张表）

| 做什么 | 在哪边 | 怎么同步 |
|---|---|---|
| Keil 工程、计划文档、代码 | **Windows** | git（SSH 免密） |
| ROS2 包源码（`ros2_ws_src/uart_bridge`） | 两边都能改 | git |
| **ROS2 编译与运行** | **只能在 Linux（ext4）** | `git pull` → `colcon build` |

> **铁律**：编译工作空间**必须放 Linux 的 ext4**（`~/ros2_ws`），**永远不要**在 NTFS（Windows 分区）上 `colcon build`。

### 0.1 GitHub 仓库分工（2026-09-19 定，**3 个库，别混**）

| 仓库 | 放什么 | 谁在用 |
|---|---|---|
| **`HzEgw/stm32_study`** | STM32 工程 01~06（标准库+寄存器双版本）+ `成长路线\` 文档 + **`ros2_ws_src/uart_bridge`**（**自研**：与固件配套的串口桥，跟着固件一起演进） | Windows + Ubuntu |
| **`HzEgw/ros2_study`** | 《动手学ROS2》**教程练习**（`chapt1~chapt10`，原作者 鱼香ROS，README 需注明出处）+ 自己的实验与笔记 | Ubuntu |
| `HzEgw/stm32-ros2-lidar-car` | **比赛作品库**（**W13 之后建**，即 F407/micro-ROS 阶段）：自研 F407 固件 + 自研 ROS2 包 | W13 后 |

> ⚠️ **两条纪律**：
> 1. **教程代码 ≠ 你的作品** —— 比赛/面试材料里只用**自己写的**代码（`chapt*` 是教材）。
> 2. **每个库的默认分支统一用 `main`**（新建库时注意；`ros2_study` 目前是 `master`，建议改）。


---

## 0.2 用哪个 Linux 环境：**双系统为主**（2026-09-19 定案）

> **决定**：**只用双系统 Ubuntu**（WSL2 不采用）。
> **理由**：本项目后面要接**真串口 / 雷达 / 树莓派 / Gazebo GPU** —— 这些在 WSL 里都要额外折腾（USB 转发、`/mnt/d` 编译慢、GPU 受限）；
> 双系统是"一次重启换全套真实环境"，更省心。
> **代价**：切换要重启 1~2 分钟 → 所以核心变成 **减少切换次数**（见 §0.3）。

| 环境 | 什么时候用 | 能干什么 | 局限 |
|---|---|---|---|
| **WSL2 + Ubuntu 22.04**（就在 Windows 里，**零重启**） | **ROS2 日（2.5h）**、碎片时间 | 命令行全套、`colcon build`、写节点/包、git、`ros2 topic/node/param` 练习 | 真 USB 串口要 `usbipd-win` 转发；GPU 加速有限 |
| **双系统 Ubuntu 22.04** | **周六攻坚日 / 周日产出日**（大块时间） | `rviz2` / Gazebo 仿真、真串口/CAN、性能活 | 切换要重启 |
| 树莓派 / 车载机（**10 月内购买**，见 `00` §11 P1） | W13 之后实车联调 | 真机跑 ROS2 + 真硬件 | — |

### WSL2 方案（**本机已决定不采用**，仅留档备查）

> ⚠️ 哪天不方便重启（例如出门只带笔记本），可按下面步骤应急；**但日常主线是双系统**。

```powershell
# Windows PowerShell（管理员）
wsl --list --online              # 看看有哪些发行版
wsl --install -d Ubuntu-22.04    # 安装（会要你设一个 Linux 用户名/密码，随便设，记牢）
wsl                              # 装完直接进
```
> 若下载慢：重试，或 `wsl --install -d Ubuntu-22.04 --web-download`。

```bash
# 在 WSL 里：装 ROS2 Humble（走国内源，免梯子）
wget http://fishros.com/install -O fishros && bash fishros    # 菜单：1 安装 ROS2 → humble → 换源
```

```bash
# 在 WSL 里：git + SSH（**再配一把钥**，Title 写 `WSL-Ubuntu`）
git config --global user.name "HzEgw"
git config --global user.email "1765377619@qq.com"
ssh-keygen -t ed25519 -C "1765377619@qq.com"     # 三次回车（密码留空）
cat ~/.ssh/id_ed25519.pub                        # → 粘到 GitHub → New SSH key
mkdir -p ~/repos && cd ~/repos && git clone git@github.com:HzEgw/stm32_study.git
```

### 两边的边界（**这三条很重要**）

1. **不共享 `~/ros2_ws`**：WSL 和双系统各自独立 `colcon build`（编译产物不跨系统）。
2. **共享的是 git 仓库**：同一份源码，靠 `git pull/push` 同步。
3. **切系统前的固定动作**：
   ```bash
   # Windows 侧（重启前）：
   git status ; git add . ; git commit -m "..." ; git push     # ← 别带着未提交改动重启
   # Linux 侧（进系统后第一件事）：
   cd ~/repos/stm32_study && git pull
   ```

### 0.3 双系统下"减少重启"的排法（**比装 WSL 更重要**）

| 规则 | 说明 |
|---|---|
| ① **只在 Linux 侧任务 ≥1h 时才切换** | 30 分钟的碎片活不值得重启（来回 ~4 分钟 + 打断思路） |
| ② **进系统前先在 Windows 写好"Linux 待办清单"** | 免得进去发呆；模板见下 |
| ③ **进去就批量做完 + 立刻 `git push`** | 别在 Linux 里写文档/查资料（那些回 Windows 做） |
| ④ **Windows 侧只做"Windows 才能做"的** | Keil 编译、固件代码、ST-Link 下载、文档、看教程、规划 |

**任务归属（照着分，切换次数自然就少了）**：

| 只在 Windows 做 | 只在 Linux 做 | 两边都能做 → **一律在 Windows 做** |
|---|---|---|
| Keil 编译/下载、ST-Link、固件代码、计划文档、看教程、写报告 | `colcon build`、跑 ROS2 节点、`ros2 bag`、rviz2 / Gazebo、（将来）真串口与实车联调 | 读/改源码、git、查资料 |

**Linux 待办清单模板**（进 Ubuntu 前在 Windows 填好，进去照抄）：

```markdown
# Linux 待办（2026-09-__）
- [ ] cd ~/repos/stm32_study && git pull
- [ ] cd ~/ros2_ws && colcon build --symlink-install --packages-select uart_bridge
- [ ] source install/setup.bash && ros2 run uart_bridge uart_bridge_node
- [ ] ros2 topic list | grep mcu              # 验收点
- [ ] （需要录数据时）ros2 bag record -o ~/bags/xxx /mcu/frame
- [ ] git status && git add . && git commit -m "..." && git push    ← 收工必做
```

> **时间盒怎么排**：周二/周四 ROS2 日（**周二 2.5h / 周四 3h**）→ **任务不足 1h 就攒到周末**；
> 周六 **4h** / 周日 **5h** → **一次进 Ubuntu 把大活批量做完**。这样一周大约只重启 **1~2 次**。

---


## 1. 一次性配置（约 5 分钟）

### 1.0 ⚠️ 先对齐双系统时钟（做一次，否则时间会偏 8 小时 —— 2026-09-23 补）

**症状**：从 Ubuntu 切回 Windows 后，**Windows 时间偏 +8 小时**（于是文件修改时间、编译日志全偏），"什么时候做的"就不可信了。
**原因**：**Ubuntu 默认把硬件时钟 RTC 当 UTC 写回，Windows 默认把 RTC 当"本地时间"读** —— 同一个 RTC，两种解释。

**治本（在 Ubuntu 里执行一次，推荐）**：
```bash
timedatectl set-local-rtc 1 --adjust-system-clock   # 让 RTC 存"本地时间" → Windows 不再跳
timedatectl status                                  # 确认 "RTC in local TZ: yes"
```
**替代方案（在 Windows 里做）**：注册表 `HKLM\SYSTEM\CurrentControlSet\Control\TimeZoneInformation` 下把 `RealTimeIsUniversal` 设为 `1`（让 Windows 也按 UTC 读 RTC），重启生效。
**临时办法（Windows）**：设置 → 时间和语言 → 「立即同步」；或管理员 PowerShell 执行 `w32tm /resync`。

> **判据**：切回 Windows 后，Windows 时间与手机一致 = 对齐成功。
> **规矩（`03` §1「时间证据分级」）**：**文件 mtime / 编译日志可能偏 8 小时** → 判断时间**一律以 git 提交时间为准**（服务端 `pushed_at`/`events` 可验证）。


### 1.1 git 身份

```bash
git config --global user.name  "HzEgw"
git config --global user.email "1765377619@qq.com"
git config --global init.defaultBranch main
```
> ⚠️ **不要**手动设 `core.autocrlf` —— 仓库里有 `.gitattributes` 统一换行符，手动设反而添乱。

### 1.2 SSH 密钥（这台机器一把，Title 建议 `Ubuntu-ROS2`）

```bash
ssh-keygen -t ed25519 -C "1765377619@qq.com"   # 一路回车；Enter passphrase 直接回车（留空！）
cat ~/.ssh/id_ed25519.pub                       # 复制这一整行
```
→ 打开 https://github.com/settings/ssh/new → Title：`Ubuntu-ROS2` → Key 粘贴 → **Add SSH key**

**验证**：
```bash
ssh -T git@github.com        # 期望：Hi HzEgw! You've successfully authenticated...
```

**若 22 端口不通**（换了学校/家里的网络可能不一样）→ 改走 443：
```bash
mkdir -p ~/.ssh && cat >> ~/.ssh/config <<'EOF'
Host github.com
  HostName ssh.github.com
  Port 443
  User git
EOF
chmod 600 ~/.ssh/config
ssh -T git@github.com
```

> ⚠️ Linux 上生成密钥时**不要用 `-N` 参数**（Windows/PowerShell 那个坑在 bash 里没有，但**留空回车更省事、更不容易错**）。

### 1.3 克隆仓库

```bash
mkdir -p ~/repos && cd ~/repos
git clone git@github.com:HzEgw/stm32_study.git
```
> 约 600 KB（11 个 Keil 工程 + 6 篇文档），秒下。

---

## 2. 国内镜像源（**省掉梯子的关键**）

### 2.1 apt → 清华源
```bash
sudo sed -i 's|//.*archive.ubuntu.com|//mirrors.tuna.tsinghua.edu.cn|g; s|//.*security.ubuntu.com|//mirrors.tuna.tsinghua.edu.cn|g' /etc/apt/sources.list
sudo apt update
```

### 2.2 pip → 清华源
```bash
pip config set global.index-url https://pypi.tuna.tsinghua.edu.cn/simple
```

### 2.3 rosdep → **rosdepc**（鱼香ROS 版，走国内源）
```bash
sudo apt install -y python3-pip
sudo pip install rosdepc
sudo rosdepc init && rosdepc update
```
> **为什么不用官方 `rosdep`**：它要从 `raw.githubusercontent.com` 拉索引，国内基本必然失败。
> `rosdepc` 是鱼香ROS 的国内替代，命令与 `rosdep` 完全一致，**把 `rosdep` 换成 `rosdepc` 即可**。

### 2.4 ROS2 本体（若还没装，或装坏了）
```bash
wget http://fishros.com/install -O fishros && bash fishros
# 菜单选择：1) 安装 ROS2 → humble → 换源用「鱼香/清华」
```

---

## 3. 编译 `uart_bridge`（W1 的 R 线产出）

包规格（已确认）：**ament_cmake + C++17**，节点源 `src/uart_bridge_node.cpp`，依赖 `rclcpp`、`std_msgs`，另装 `launch/` 目录。

```bash
# ① 建工作空间，并用【软链接】把仓库里的包接进来（改一处，两边同步）
mkdir -p ~/ros2_ws/src && cd ~/ros2_ws/src
ln -s ~/repos/stm32_study/ros2_ws_src/uart_bridge uart_bridge

# ② 装依赖（国内源）
cd ~/ros2_ws
rosdepc install -i --from-path src --rosdistro humble -y

# ③ 编译（--symlink-install：改代码不用重编）
source /opt/ros/humble/setup.bash
colcon build --symlink-install --packages-select uart_bridge

# ④ 运行
source install/setup.bash
ros2 run uart_bridge uart_bridge_node
```

> 为什么用软链接而不是把整仓克隆进 `src/`：仓库里还有 11 个 Keil 工程，colcon 会白扫一遍；
> 软链接只把**真正的 ROS2 包**接进来，干净且省时间。

### 3.1 双系统下"串口"到底在哪边？（2026-09-23 补 · 回答"可是我是双系统啊"）

> **一句话**：**串口永远属于"正在跑 ROS2 的那个系统"= Ubuntu**。
> `socat` 本身就是 Linux 工具（Windows 上根本没有），所以"假数据合流"**本来就只在 Ubuntu 里做** ——
> **不需要 STM32、不需要 Keil、不需要动 Windows**。→ **双系统不构成任何障碍。**

**① 设备名是"每系统一套"**（同一根 USB-TTL，换个系统就换个名字）：

| 系统 | 名字 | 要点 |
|---|---|---|
| **Ubuntu** | `/dev/ttyUSB0`（CH340/CP2102 类）· `/dev/ttyACM0`（ST-Link 虚拟串口） | 用户须在 **`dialout`** 组：`sudo usermod -aG dialout $USER` → **重新登录**才生效 |
| Windows | `COM3` / `COM5`（设备管理器里看） | Keil 下载/串口助手用的就是它 |

→ `uart_bridge` 的 **`port` 参数只填 Ubuntu 下的名字**（写 `COM3` 对它毫无意义）；
→ 双系统**同一时刻只有一个系统在跑** → **不存在"两个系统抢串口"**。

**② 假数据合流（4 个终端 · 全程 Ubuntu · 约 5 分钟 · 不需要任何硬件）**

```bash
sudo apt install -y socat                    # 一次性

# 终端 A：造一对虚拟串口（互相对通），保持不关
socat -d -d pty,raw,echo=0 pty,raw,echo=0
#   ↑ 它会打印两行 /dev/pts/N（例：/dev/pts/5 与 /dev/pts/6）→ 下面按这个改
```
```bash
# 终端 B：让桥去开"另一头"（顺序：先 socat，再起桥，最省事）
source ~/ros2_ws/install/setup.bash
ros2 run uart_bridge uart_bridge_node --ros-args -p port:=/dev/pts/6 -p baud:=115200
#   日志应出现：串口已打开: /dev/pts/6 @ 115200 8N1
#   等价的 launch 写法（`port`/`baud` 已在 launch 文件里声明为启动参数 ✅）：
#   ros2 launch uart_bridge uart_bridge.launch.py port:=/dev/pts/6 baud:=115200
```
```bash
# 终端 C：看话题
ros2 topic echo /mcu/frame
ros2 topic echo /mcu/counter
```
```bash
# 终端 D：往"你这一头"灌一帧【合法】数据
#   帧格式：0xAA 0x55 | LEN | PAYLOAD[LEN] | SUM，SUM=(LEN+ΣPAYLOAD)&0xFF
#   取 payload = 00 07 → LEN=0x02，SUM=(0x02+0x00+0x07)&0xFF=0x09
printf '\xAA\x55\x02\x00\x07\x09' > /dev/pts/5
```

**验收（这就是"桥通了"的证据）**：
- `/mcu/frame` 打印 `data: [0, 7]`
- `/mcu/counter` 打印 `data: 7`
- **反证**：故意把 SUM 写错（`printf '\xAA\x55\x02\x00\x07\x08'`）→ **话题不动**，桥的日志里"校验错"计数 +1

> ⚠️ **三个坑（前两个你 09-24 实测踩过）**：① `socat` 那个终端**不能关**（一关，两个 `/dev/pts/N` 就消失）；② **`/dev/pts/N` 的编号每次启动都会变**（实测是 **21/22** —— 示例里的 `5/6` 只是示意）→ **别照抄编号，先看 `socat` 打印的那两行再填**；③ `openPort()` 在节点**构造时**就调用（源码），所以**先起 `socat`、再起桥** —— 顺序倒了会看到"打开串口失败"，重启桥即可（源码里有 `retry_` 重试计数，但别赌它）。
> 🔍 **症状速查（09-24 实测）**：**终端里突然飘出一串 `U`** ⇒ 你把帧写到**终端自己**那边的 pts 去了（`0x55` 正好是 ASCII 的 `'U'`）→ **把两个编号对调**再灌一次即可。
> 📌 这是 **W2 的加餐**（`00` §16.8 v1.23 ②）：**纯 PC 侧**、不引入任何 STM32 新外设 → 不算跳步。
> ⏱ **但别为它单独重启**（`04` §0.3 规则①：Linux 侧任务 **<1h 不切换**）→ **并进你下一次进 Ubuntu 的大块时间**（W2 周六/周日），和"加 `publish_period` 参数 + launch + bag"一起做完。

**③ 真合流（W6 起）在双系统下的固定流程 —— 这条必须提前知道**

ROS2 只在 Ubuntu 跑 → **MCU 的 USB 必须插在 Ubuntu 侧**；而 Keil 烧写/调试在 Windows 侧。
于是"改固件"和"看数据"分属两个系统 → **每次真合流都要重启一次**。省时间的做法：

1. **Windows 侧**：把固件做成**上电即自动发帧**（不依赖调试器、不依赖断点）；
2. **Windows 侧烧好 → 关机重启进 Ubuntu**（同一个串口不可能被两个系统同时占着）；
3. **Ubuntu 侧**：`ls -l /dev/ttyUSB*` 确认名字 → `ros2 run uart_bridge uart_bridge_node --ros-args -p port:=/dev/ttyUSB0` → **看到 `/mcu/frame`/`/mcu/counter`**（这一步就是 W6 的验收）。

> （可选替代：Ubuntu 里用 `st-flash`/OpenOCD 也能烧写 —— 但**违反"一次只引入一条新知识"**，W6 之前不折腾。）

---

## 4. 验收清单（W1 周日 R 线的 4 个产出）

```bash
ros2 doctor                     # ① 环境体检（有 error 就先解决，warning 可放过）

ros2 topic list                 # ② 命令肌肉记忆：五条命令逐个过一遍
ros2 topic hz /rosout           #    （找现成的节点练手）
ros2 topic echo /rosout --once
ros2 node list
ros2 param list

ros2 run uart_bridge uart_bridge_node     # ③ 桥节点能启动（没有串口也应打印"打开失败"的清晰错误）
ros2 topic list | grep mcu               # ④ 能看到 /mcu/frame 话题
```

**未完成判据**：① `ros2 doctor` 无 error 　② 五条命令不看笔记能说出作用 　③ 包能编译能跑 　④ `/mcu/frame` 出现。

---

## 5. 常见报错（对着查）

| 报错 | 原因 | 解决 |
|---|---|---|
| `Permission denied (publickey)` | 本机这把钥没加到 GitHub | §1.2（别把 Windows 的私钥拷过来，**每台机器一把**） |
| `fatal: unable to access 'https://github.com/...'` | 走的是 HTTPS 且 443 被墙 | 换 SSH：`git remote set-url origin git@github.com:HzEgw/stm32_study.git` |
| `ssh: connect to host github.com port 22: Connection timed out` | 22 被挡 | §1.2 的 **443 隧道**配置 |
| `rosdep: command not found` / `rosdep update` 卡死 | 官方 rosdep 拉不到索引 | 用 **`rosdepc`**（§2.3），命令同名替换 |
| `Could not find package 'uart_bridge'` | 没编或没 source | `colcon build --packages-select uart_bridge` → `source install/setup.bash` |
| `find_package(rclcpp)` 失败 / `ament_cmake` 找不到 | 没先 source ROS2 | `source /opt/ros/humble/setup.bash` 后再 build |
| colcon 报权限/软链接错误 | 工作空间建在了 NTFS/Windows 分区 | 移到 `~/ros2_ws`（ext4） |
| push 时提示 `LF will be replaced by CRLF` | 不用管，`.gitattributes` 已统一 LF | — |

---

## 6. 每次开工/收工的固定动作

```bash
# Linux 侧 · 开工前：拉最新
cd ~/repos/stm32_study && git pull

# Linux 侧 · 收工前：把改动推上去
git status ; git add . ; git commit -m "feat: uart_bridge 参数化" ; git push
```

```powershell
# Windows 侧 · 收工前：四连（这才是"事实"）
git status ; git add . ; git commit -m "docs: xxx" ; git push
```

> **一句话记住**：**源码用 git 同步，编译只认 Linux ext4**。
> 文档/Keil 工程在 Windows 写，ROS2 在 Ubuntu 编 —— 两边永不"手动拷贝"。

