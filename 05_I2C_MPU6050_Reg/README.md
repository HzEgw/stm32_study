# 05_I2C_MPU6050_Reg —— I2C + 六轴姿态（**寄存器版** · W3 主线）

> 功能与标准库版 `05_I2C_MPU6050` **完全相同**，差别只在"驱动层直接写 I2C1 的寄存器"。
> **建议左右分屏对照阅读**：`mpu6050.c`（标准库）↔ `mpu6050_reg.c`（寄存器），文件顶部的对照表一一对应。

---

## 1. 学到的知识点（寄存器视角）

| 知识点 | 寄存器级要点 |
|---|---|
| I2C 物理层 | PB6/PB7 必须**复用开漏**（`GPIOB->CRL` 的 bit31:24 = `0xFF`）+ 外部/模块自带 4.7k 上拉 |
| I2C 时序参数 | `CR2.FREQ = PCLK1(MHz)=36`；`CCR = PCLK1/(3×SCL)=30`（快速模式 2:1）；`TRISE = 12` |
| **START / STOP 的位** | ★ `CR1` 的 **bit8 = START**、**bit9 = STOP**（bit6 是 ENGC，最容易记错！） |
| 事件等待（EV） | EV5 等 `SR1.SB` → EV6 等 `SR1.ADDR` → EV8 等 `SR1.TXE` → EV8_2 等 `TXE|BTF` → EV7 等 `SR1.RXNE` |
| **清 ADDR / 清 AF** | 清 ADDR：**先读 SR1 再读 SR2**；清 AF：**向 AF 位写 0**，并且要发 STOP 释放总线 |
| 总线忙判定 | ★ `SR2` 的 **bit1 = BUSY**（不是 bit6） |
| 读多字节收尾 | 最后 1 字节前 **关 ACK（`CR1 &= ~ACK`）+ 发 STOP**，否则会多读 |
| **全程超时** | 每个 `while` 都带计数超时；超时→记错误→清 AF→发 STOP，**绝不死等** |
| 定时器寄存器 | `PSC/ARR` 定频，`CCMR2` 的 `OC3M=110` + `OC3PE`，`CCER.CC3E`，`CR1.ARPE|CEN`，`EGR.UG` |
| 分层 | 底层（i2c_* 静态函数）↔ 器件层（MPU_* 接口）↔ 应用层（main 里的采样/滤波循环） |

## 2. 接线（与标准库版相同）

| MPU6050 模块 | 接到 STM32F103C8 | 说明 |
|---|---|---|
| VCC | **3.3V** | 别接 5V |
| GND | GND | 共地 |
| SCL | **PB6** | I2C1_SCL |
| SDA | **PB7** | I2C1_SDA |
| AD0 | GND | 地址 = **0x68**（接 VCC 则 0x69，改 `MPU6050_ADDR`） |
| INT | 不接 | 本工程轮询读取 |

| 观察脚 | 说明 |
|---|---|
| **PB0** | TIM3_CH3 输出与 pitch 成正比的 1kHz PWM（万用表：水平 ≈1.65V） |

## 3. Keil 建工程（⚠️ 与标准库版不同！）

| 分组 | 文件 |
|---|---|
| `USER` | `USER\main.c`、`USER\stm32f10x_it.c` |
| `HARDWARE` | `HARDWARE\mpu6050_reg.c` |
| `CMSIS` | `Libraries\CMSIS\...\system_stm32f10x.c` + `startup_stm32f10x_md.s` |

- **Options → C/C++ → Define 只写**：`STM32F10X_MD`
  （寄存器工程**不要**定义 `USE_STDPERIPH_DRIVER`，否则会去包含 `stm32f10x_conf.h`）
- Include Paths **4 条**：`USER`、`HARDWARE`、
  `Libraries\CMSIS\CM3\CoreSupport`、
  `Libraries\CMSIS\CM3\DeviceSupport\ST\STM32F10x`
- **FWLIB 分组：一个 .c 都不用加**（全程不调用标准库函数）
- 勾选 **MicroLIB**（`atan2f/sqrtf` 不会报链接错误；不想用它就把滤波换成小角度近似，见常见问题）
- Debug = ST-Link/SWD

## 4. 标准库 ↔ 寄存器 对照表（本工程的全部映射）

| 标准库版写法 | 寄存器版写法 | 备注 |
|---|---|---|
| `RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB\|AFIO, ENABLE)` | `RCC->APB2ENR \|= (1<<0)\|(1<<3)` | bit0 AFIOEN, bit3 IOPBEN |
| `RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE)` | `RCC->APB1ENR \|= (1<<21)` | I2C1EN |
| `GPIO_Init(..., GPIO_Mode_AF_OD)` | `GPIOB->CRL \|= 0xFF000000` | PB6=bit27:24, PB7=bit31:28 |
| `I2C_Init(I2C1, &s)` | `CR2=FREQ`、`CCR`、`TRISE`、`CR1\|=PE` | 见文件头速查表 |
| `I2C_Cmd(I2C1, ENABLE)` | `I2C1->CR1 \|= (1<<0)` | PE |
| `I2C_GenerateSTART(I2C1, ENABLE)` | `I2C1->CR1 \|= (1<<8)` | START |
| `I2C_GenerateSTOP(I2C1, ENABLE)` | `I2C1->CR1 \|= (1<<9)` | STOP |
| `I2C_Send7bitAddress(I2C1, dev, dir)` | `I2C1->DR = (dev<<1) \| dir` | 寄存器版用 7 位地址 |
| `I2C_CheckEvent(...)` | `i2c_wait_sr1(位掩码)` + `i2c_clear_addr()` | 超时自己数 |
| `I2C_SendData / I2C_ReceiveData` | `I2C1->DR = x / y = I2C1->DR` | — |
| `I2C_AcknowledgeConfig(I2C1, DISABLE)` | `I2C1->CR1 &= ~(1<<10)` | 读最后 1 字节前 |
| `TIM_TimeBaseInit / TIM_OC3Init / TIM_Cmd` | `TIM3->PSC/ARR/CCMR2/CCER/CR1/EGR` | PB0 角度 PWM |
| `TIM_SetCompare3(TIM3, ccr)` | `TIM3->CCR3 = ccr` | — |

---

## 5. 验证（按下表逐条过，左侧是 Watch 变量）

| 步骤 | Watch 变量 | 期望值 | 不符合时看哪 |
|---|---|---|---|
| ① I2C 已使能 | `g_reg_pe` | **1** | `CR1.PE` 没置位 |
| ② 时钟参数 | `g_reg_cr2` | **0x0024**（FREQ=36） | PCLK1 不是 36MHz？确认 `SystemInit` 跑了（72MHz） |
| ③ 速率参数 | `g_reg_ccr` | **30**（400kHz） | 改过 `I2C_REG_CLOCK_HZ`？100k 时应是 180 |
| ④ 上拉时间 | `g_reg_trise` | **12** | 同上 |
| ⑤ 从机地址寄存器 | `g_reg_oar1` | **0x4000** + 地址 | bit14 必须保持 1（手册要求） |
| ⑥ **器件识别** | `g_whoami` | **0x68** | 接线/上拉/AD0/VCC；见第 6 节 |
| ⑦ I2C 扫描 | `g_scan_cnt` / `g_scan_list[0]` | **≥1** / **0x68** | 0 → 总线没通（上拉、接线、供电） |
| ⑧ 通信无错 | `g_i2c_err` | **0**（长时间保持） | 400k 下上拉不足 → 改成 100k 试 |
| ⑨ 原始数据 | `g_az` 静止水平 | 约 **+8192**（1g） | 量程配置与换算不一致 |
| ⑩ 姿态 | `g_pitch` / `g_roll` | 倾斜跟随、静止稳定 | 漂移大 → 做练习 1 的零偏校准 |
| ⑪ 输出可视化 | 万用表量 PB0 | 水平 ≈**1.65V** | 看 `g_reg_ccr3` 是否在变 |
| ⑫ 空闲寄存器 | `g_reg_sr2` | **bit1(BUSY) = 0** | 一直为 1 → 总线被拉死（AF 没清） |

**验收标准（DoD）**：`g_whoami = 0x68`、`g_scan_cnt ≥ 1`、`g_i2c_err` 长时间为 0、倾斜时 `g_pitch` 跟随、静止无明显漂移。

## 6. 常见问题（**寄存器版专属的坑**排在最前）

| 现象 | 原因与解决 |
|---|---|
| 一上电 `g_reg_sr2` 的 BUSY 一直是 1、`g_i2c_err` 猛涨 | ① START/STOP 位记错（必须是 bit8/bit9）② 超时后**忘了清 AF + 发 STOP**（本工程已做，若你改过代码要检查） |
| `g_whoami = 0x00` | 未上拉 / VCC 没接 / SCL-SDA 接反 / AD0 接 VCC（地址变 0x69） |
| `g_whoami = 0xFF` | 总线被拉死或器件不应答（AF 置位）→ 检查接线，`I2C_REG_CLOCK_HZ` 降到 100000 试 |
| 读出来的数据整体偏一位 | 连续读时**没有"先关 ACK 再发 STOP"** → 多读了 1 字节 |
| 偶发读到 0 或数据错乱 | 忘了清 ADDR（必须先读 SR1 再读 SR2），或 EV8_2 没等 `TXE|BTF` 就发了 RESTART |
| `g_i2c_err` 偶尔 +1 但功能正常 | 400kHz 下杜邦线太长/上拉弱 → 降到 100kHz + 缩短线 |
| Keil 链接报 `atan2f/sqrtf` 未定义 | 勾选 MicroLIB；或改成小角度近似 `pitch_acc = -ax * 57.29578f;`（±30° 内够用） |
| 编译报 `I2C1` 未定义 | 忘了在 Define 里写 `STM32F10X_MD`（它决定 `stm32f10x.h` 里选哪套头文件） |
| PB0 没有输出 | `CCER` 的 CC3E(bit8) 没置位 / `CR1.CEN` 没置位 / 忘了 `EGR.UG` 让 PSC/ARR 立即生效 |

## 7. 练习（做完这些才算真会寄存器版）

1. **陀螺零偏校准**：开机静止 1 秒累计 200 次，求平均作为 `gyro_bias` 扣掉（平衡车必做）；
2. **读温度**：`buf[6..7]` → `T = raw/340 + 36.53`，顺便验证"连续读 14 字节"的收尾处理；
3. **改速率**：把 `I2C_REG_CLOCK_HZ` 改成 100000，自己算一遍 `CCR/TRISE` 该是多少（校验 180 / 37），再实测；
4. **加超时统计**：把 `i2c_wait_sr1` 里"等待了多少次循环"也记下来，对比 400k 与 100k 的差异；
5. **I2C 多设备**：把 OLED（0x3C）挂到同一条总线，让 `g_scan_list` 同时出现 `0x68` 与 `0x3C`
   —— 这正是车上真实拓扑（FishBot 官方板 IMU 与 OLED 共用一条 I2C）；
6. **对比标准库版**：同样跑 10 分钟，比较两版的 `g_i2c_err`、CPU 占用（可用示波器看主循环翻转脚）。

## 8. 下一步

- 本工程 + `03_Encoder_Reg`（编码器测速）= **平衡车的两条腿**（姿态 + 速度）；
- W3 收尾：用平衡车套件做**直立环 PID**（`06_Kit_Check` 负责到货验收，已提前写好）；
- W8 之后：把这里的 `i2c_*` 底层换成 **DMA + 中断**，就是"自研固件"相对官方 Arduino 版的对比亮点之一。