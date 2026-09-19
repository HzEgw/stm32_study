# 05 · git 速查与工作流（零基础 → 够用）

> 为什么必须学：① 你有 3 人团队，代码必须能合；② 双系统（Windows 写代码 / Linux 编译 ROS2）**靠 git 同步**；
> ③ 挑战杯/面试要看"你有没有工程习惯"，commit 历史就是证据。
> 目标：**1 小时能开始用，一周形成习惯**。

## 0. 先纠正三个误区（2026-09-19 实盘踩坑记录）

> 这三个是初学者第一天最容易搞错的 —— **认错"仓库长什么样"，会白白浪费一晚上**。

| 误区（很自然的直觉） | 事实 |
|---|---|
| ❌ "我要建一个叫 `git` 的文件夹，把项目放进去，它就成仓库了" | ✅ **仓库 = 项目目录里那个隐藏的 `.git`（带点！）**，由 `git init` 自动生成。项目文件夹**名字永远不用改、也永远不会叫 git** |
| ❌ "`.git` 是个我可以手动管理的普通文件夹" | ✅ 它是 **git 的数据库**（`objects/` 存快照、`refs/` 存分支、`HEAD` 指向当前分支）。**不要手改手删里面的东西**；想彻底撤销这个仓库，就是删掉整个 `.git`（`git` 命令仍会正常工作，只是变成"非仓库"目录） |
| ❌ "得先把 GitHub 仓库建好，本地才能开始" | ✅ 正确顺序：**本地 `git init` → `commit`（先有历史）→ 再去 GitHub 建**空**仓库 → `git remote add` → `push`**。这样最不容易乱 |

**60 秒确认自己处在什么状态**（在任何目录里都能跑）：

```bash
git status                       # 报 "fatal: not a git repository" → 这里不是仓库（或还没 init）
git rev-parse --show-toplevel    # 是仓库 → 打印仓库根目录的绝对路径（认准它！）
git log --oneline -5             # 看最近 5 条提交；空仓库会提示 "does not have any commits yet"
```

> 一句话记住：**`git init` 之前，文件夹只是文件夹；`git init` 之后，它才"变成"仓库 ——
> 变的不是名字，是里面多了一个 `.git`。**

---


## 1. 一次性配置（只做一次）

```bash
git config --global user.name  "你的名字"
git config --global user.email "你的邮箱"        # 建议与 GitHub 一致
git config --global init.defaultBranch main
git config --global core.autocrlf false          # Windows/Linux 双系统建议 false, 避免换行符捣乱
```
GitHub 上建议用 **SSH key**（一次配置长期有效）：
```bash
ssh-keygen -t ed25519 -C "你的邮箱"        # 一路回车
cat ~/.ssh/id_ed25519.pub                  # 复制内容
# 粘贴到 GitHub → Settings → SSH and GPG keys → New SSH key
ssh -T git@github.com                      # 看到 "Hi xxx!" 就成功
```
（Windows 下在 **Git Bash** 里执行同样命令；也可用 HTTPS + Personal Access Token。）

## 2. 日常五连（90% 的时间只用这些）

```bash
git status                 # 看有哪些改动（最常用，先看再动）
git add .                  # 把改动放进暂存区（也可 git add 具体文件）
git commit -m "说明这次改了什么"    # 提交到本地仓库
git push                   # 推到 GitHub
git pull                   # 拉别人的改动（每天开工前先 pull！）
```
**提交信息写法**（以后你会感谢自己）：
`feat: 新增 UART 环形缓冲接收` / `fix: 修复 USART1 中断未清标志` / `docs: 补充 Keil 建工程步骤` / `refactor: 抽出帧解析状态机`

## 3. 分支 + PR（3 人团队的最小规范）

```bash
git checkout -b feat/uart-ringbuf      # 从 main 开新分支
# ...写代码...
git add . && git commit -m "feat: UART 环形缓冲"
git push -u origin feat/uart-ringbuf   # 推分支
# 到 GitHub 上点 "Compare & pull request" → 写说明 → Create pull request
git checkout main && git pull          # 合并后切回 main 拉最新
git branch -d feat/uart-ringbuf        # 删掉本地分支
```
> 自己一个人开发也建议走 PR：**比赛材料里截图 PR 记录，比口头说"我用了版本管理"有力得多。**

## 4. 高频报错与解决

| 现象 | 原因 | 解决 |
|---|---|---|
| `Please tell me who you are` | 没配 user.name/email | 第 1 节 |
| `! [rejected] ... (fetch first)` | 远端有新提交 | 先 `git pull --rebase`，再 `git push` |
| `CONFLICT (content): Merge conflict in xxx` | 同一处被两人改 | 打开文件找 `<<<<<<<`/`=======`/`>>>>>>>`，改成想要的样子 → `git add xxx` → `git commit` |
| `You are in 'detached HEAD' state` | 检出了某个 commit 而不是分支 | `git checkout main` |
| 提交里出现不该有的文件 | 没写 `.gitignore` | 建 `.gitignore`（见第 5 节），再 `git rm -r --cached 文件名` |
| 提交信息写错/漏文件 | 想改最后一次提交 | `git commit --amend`（已 push 的要谨慎） |
| 想撤销工作区改动 | — | `git restore 文件名`（**不可恢复，慎用**） |
| 不小心 commit 了密码/密钥 | — | 改文件 + 改密码；历史里的要 `git filter-repo` 清（麻烦，所以**不要提交密钥**） |
| 大文件（>100MB）推不上去 | GitHub 限制 | 别把 `.uvguix`/`Objects/`/数据集 提交；大文件用 Git LFS 或网盘 |

## 5. `.gitignore` 模板

**STM32 / Keil 工程**
```gitignore
# 编译产物
Objects/
Listings/
DebugConfig/
*.o  *.d  *.axf  *.hex  *.bin  *.map  *.htm  *.lnp  *.lst  *.crf  *.dep
*.uvguix.*          # Keil 的窗口布局（每人不同，别提交）
RTE/                # Keil RTE 自动生成
# 说明: Libraries/ 是否提交自己定 —— 提交可保证"别人 clone 就能编译"(推荐);
#       不提交则体积小, 但需在 README 写清怎么补库。
```

**ROS2 工作空间**
```gitignore
build/
install/
log/
*.pyc
__pycache__/
```

## 6. 双系统怎么用（我们的方案）

| 步骤 | Windows 侧 | Linux 侧 |
|---|---|---|
| 写 STM32 固件 | `git add/commit/push`（Git Bash 或 VS Code 的 Git 面板） | — |
| 写 ROS2 包 | 同样提交源码（**不要提交 build/install/log**） | `cd ~/ros2_ws/src && git pull`，然后 `cd ~/ros2_ws && colcon build --symlink-install` |
| 看笔记 | 直接在 VS Code 里看 | 想看得舒服就只读挂载 NTFS（不要拿去编译） |

## 7. 今天就能做完的三步（对应 W1）

```bash
# 在工作区根目录
cd /d/STM32F103C8_Workspace          # Windows Git Bash 路径写法
git init
# 建 .gitignore（把上面 STM32 部分粘进去）
git add .
git commit -m "chore: 初始化工作区(工程 + 笔记 + 计划)"
# 在 GitHub 新建空仓库 stm32-ros2-lidar-car, 然后:
git remote add origin git@github.com:你的用户名/stm32-ros2-lidar-car.git
git push -u origin main
```
> 建议先建**私有**仓库，等选题书定稿、比赛材料准备好再公开（见计划第 8 节待确认第 2 条）。

## 8. 进阶（以后需要再看）

- `git log --oneline --graph --all`：看提交历史图（截图放报告里很专业）
- `git stash`：临时切换任务时暂存改动
- `git tag v0.1`：给比赛提交的版本打标签（**"参赛版本"要能一键找回**）
- `git bisect`：定位"哪次提交引入的 bug"
- GitHub Actions：自动编译检查（STM32 用 `arm-none-eabi-gcc`，可以在 Windows/WSL 跑）
