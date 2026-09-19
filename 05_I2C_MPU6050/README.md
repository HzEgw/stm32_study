# 05_I2C_MPU6050 —— I2C + 六轴姿态（标准库版 · W3 主线）

> 独立工程，自带 `Libraries/`。**这是你唯一缺的外设——I2C**，也是平衡车/FishBot 的 IMU 基础。
> 寄存器版见 `05_I2C_MPU6050_Reg`（下一轮生成）。

---

## 1. 学到的知识点

| 知识点 | 说明 |
|---|---|
| **I2C 物理层** | SCL/SDA **必须开漏 + 上拉**（`GPIO_Mode_AF_OD` + 外部 4.7k，模块板一般自带） |
| I2C 时序 | START → 地址+W → 寄存器号 → RESTART → 地址+R → 连续读 → **最后 1 字节先关应答再 STOP** |
| **事件等待 + 超时** | EV5/EV6/EV7/EV8 全都要等，但**必须带超时**（否则总线一挂程序死等） |
| 器件驱动分层 | 底层（I2C 读写寄存器）↔ 器件层（MPU6050 初始化/读原始值）↔ 应用层（姿态解算） |
| 原始值换算 | 加速度 ±4g → 8192 LSB/g；陀螺 ±500°/s → 65.5 LSB/(°/s)；数据 **大端**(高字节在前) |
| **互补滤波** | `angle = 0.98×(angle + 陀螺积分) + 0.02×加速度倾角`（静态准 + 动态准的融合） |
| 无仪表验证 | 把角度映射到 **PB0 的 PWM**，万用表就能看 |

## 2. 接线（MPU6050 模块 → STM32F103C8）

| 模块引脚 | 接到 | 说明 |
|---|---|---|
| VCC | **3.3V** | 别接 5V（多数模块能耐受，但 3.3V 最稳） |
| GND | GND | 共地 |
| SCL | **PB6** | I2C1_SCL（复用开漏，无需重映射） |
| SDA | **PB7** | I2C1_SDA |
| AD0 | GND | 决定地址：接 GND = **0x68**，接 VCC = 0x69 |
| INT | 不接 | 本工程轮询读取，不需要中断引脚 |

| 观察脚 | 说明 |
|---|---|
| PB0 | TIM3_CH3 输出与 pitch 成正比的 1kHz PWM（万用表可看：水平 1.65V） |

## 3. Keil 建工程

| 分组 | 文件 |
|---|---|
| `USER` | `USER\main.c` `USER\stm32f10x_it.c` |
| `HARDWARE` | `HARDWARE\mpu6050.c` |
| `CMSIS` | `system_stm32f10x.c` + `startup_stm32f10x_md.s` |
| `FWLIB` | `misc.c` `stm32f10x_gpio.c` `stm32f10x_rcc.c` **`stm32f10x_i2c.c`** `stm32f10x_tim.c` |

Include Paths 5 条；Define `USE_STDPERIPH_DRIVER, STM32F10X_MD`；Debug = ST-Link/SW。

## 4. 验证（按下表逐条过）

| 步骤 | Watch 变量 | 期望 | 不符合时的排查方向 |
|---|---|---|---|
| ① 器件识别 | `g_whoami` | **0x68** | 供电/接线/上拉/AD0；见第 6 节 |
| ② I2C 正常 | `g_reg_pe` | **1**（I2C1 的 PE 位） | 时钟没开或 `I2C_Cmd` 没执行 |
| ③ 通信无错 | `g_i2c_err` | **0**（长时间保持 0） | 上拉不足 / 400k 太快 → 降到 100k / 线太长 |
| ④ 原始数据 | `g_az` 水平静止 | 约 **+8192**（重力 1g ≈ 8192） | 量程配置与换算是否一致 |
| ⑤ 姿态 | `g_pitch` / `g_roll` | 倾斜板子跟着变；静止时基本稳定 | 漂移大 → 见第 6 节的零偏校准 |
| ⑥ 输出可视化 | 万用表量 PB0 | 水平 ≈1.65V，倾斜时变化 | PWM 没输出？检查 TIM3 启动/CCR3 |

**验收标准（DoD）**：`g_whoami = 0x68`、`g_i2c_err` 长时间为 0、倾斜时 `g_pitch` 跟随、静止时无明显漂移。

## 5. 关键库函数 → 寄存器对照

| 标准库 | 实际寄存器 |
|---|---|
| `GPIO_Init(..., GPIO_Mode_AF_OD)` | `GPIOB->CRL` 的 bit31:24 = `0xFF`（CNF=11 复用开漏, MODE=11 50MHz） |
| `I2C_Init(I2C1, &s)` | `CR2` 的 `FREQ[5:0]=PCLK1(MHz)`；`CCR`；`TRISE` = PCLK1+1 |
| `I2C_Cmd(I2C1, ENABLE)` | `I2C1->CR1 \|= (1<<0)` **PE** |
| `I2C_GenerateSTART/STOP` | `CR1` 的 `START(bit8)` / `STOP(bit9)` |
| `I2C_Send7bitAddress(...)` | 把 (地址<<1)\|方向 写入 `DR` |
| `I2C_SendData/ReceiveData` | 写/读 `I2C1->DR` |
| `I2C_CheckEvent(...)` | 组合判断 `SR1`（SB/ADDR/RXNE/TXE/BTF）与 `SR2`（MSL/TRA/BUSY） |
| `I2C_AcknowledgeConfig(I2C1, DISABLE)` | `CR1` 清 `ACK(bit10)`（读最后 1 字节前必须做） |

## 6. 常见问题

| 现象 | 原因与解决 |
|---|---|
| `g_whoami = 0x00` | ① 没上拉（很多裸芯片需要 4.7k，模块板自带）② VCC 没接 ③ SCL/SDA 接反 ④ AD0 接 VCC 时地址变成 0x69（改 `MPU6050_ADDR` 为 0xD2） |
| `g_whoami = 0xFF` | 总线被拉死/器件没应答（AF 标志置位）→ 检查接线、降速到 100k |
| `g_i2c_err` 一直涨 | 400kHz 下上拉不够或线太长 → 改 `I2C_ClockSpeed = 100000`，缩短杜邦线 |
| 角度缓慢漂移 | 正常（陀螺零偏）；**练习 1** 就是做零偏校准 |
| 角度几乎不动 | 采样没跑（看 `g_samples` 是否增长）或 `MPU_ReadRaw` 一直失败 |
| Keil 链接报 `atan2f/sqrtf` 未定义 | ① 勾选 MicroLIB；② 或把 `MPU_UpdateAttitude` 里换成小角度近似 `pitch_acc = -ax * 57.29578f;`（±30° 内够用，注释里有说明） |
| 一上电程序就"卡住" | 说明 I2C 死等 —— 本工程已全程加超时，若仍卡，检查是否漏了 `I2C_WAIT_EVENT` 的某个事件 |

## 7. 练习（做完这些才算真会）

1. **零偏校准**：开机静止 1 秒，累计 200 次陀螺读数求平均，作为 `gyro_bias` 在积分时扣掉（这是平衡车必做的一步）；
2. **读温度**：`0x41~0x42` → `temp = raw/340.0 + 36.53`，顺便验证"连续读多字节"；
3. **串口输出曲线**：把 `04_UART_Bluetooth` 的 `uart.c` 加进来，每 20ms 发一帧 {pitch, roll}，用上位机/ROS2 画曲线（这就是 W2 桥的实战）；
4. **换算法对比**：互补滤波 vs 卡尔曼（同样数据下比较跟随速度与漂移，**这个对比能直接写进挑战杯报告的"算法对比实验"**）；
5. **加 I2C 从机地址扫描**：开机扫 0x08~0x77，把所有应答的地址放进 Watch —— 排查"器件在不在"最有效。

## 8. 下一步

- 本工程 + `03_Encoder`（编码器测速）合起来，就是**平衡车的两条腿**：姿态 + 速度；
- W3 收尾：用平衡车套件做 **直立环 PID**（`06_Kit_Check` 负责验收硬件）。
