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
| `HzEgw/stm32-ros2-lidar-car` | **比赛作品库**（W5 之后建）：自研 F407 固件 + 自研 ROS2 包 | 11 月后 |

> ⚠️ **两条纪律**：
> 1. **教程代码 ≠ 你的作品** —— 比赛/面试材料里只用**自己写的**代码（`chapt*` 是教材）。
> 2. **每个库的默认分支统一用 `main`**（新建库时注意；`ros2_study` 目前是 `master`，建议改）。


---

## 0.2 三个 Linux 环境怎么分工（**"双系统切换太麻烦"的解法**）

> **痛点**：双系统每次切换要**重启 1~2 分钟**，一天切两趟很折磨。
> **解法**：**日常用 WSL2（零重启），重活才进双系统。**

| 环境 | 什么时候用 | 能干什么 | 局限 |
|---|---|---|---|
| **WSL2 + Ubuntu 22.04**（就在 Windows 里，**零重启**） | **工作日 2h 的"ROS2 日"**、碎片时间 | 命令行全套、`colcon build`、写节点/包、git、`ros2 topic/node/param` 练习 | 真 USB 串口要 `usbipd-win` 转发；GPU 加速有限 |
| **双系统 Ubuntu 22.04** | **周六攻坚日 / 周日产出日**（大块时间） | `rviz2` / Gazebo 仿真、真串口/CAN、性能活 | 切换要重启 |
| 树莓派 / 车载机（11 月后） | W5 之后实车联调 | 真机跑 ROS2 + 真硬件 | — |

### 装 WSL2（一次性，约 10 分钟；**不需要重启**，因为 WSL 已就绪）

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

> **主题日怎么排**：周二/周四 ROS2 日（工作日 2h）→ **WSL 里做**；周六/周日（各 5h）→
> 需要 GUI（rviz2/Gazebo）或真硬件时，**重启进双系统一次，批量做完**。

---


## 1. 一次性配置（约 5 分钟）

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

