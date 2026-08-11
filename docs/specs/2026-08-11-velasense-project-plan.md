# 心迹 VelaSense 完整项目计划

> 版本: v1.0
> 日期: 2026-08-11
> 队伍: contest2026_377_moyumanbo
> 目标平台: SF32LB52 LCD (openvela)

---

## 一、项目总览

### 1.1 交付物清单

| # | 交付物 | 说明 |
|---|--------|------|
| 1 | openvela 固件 | 可烧录、可运行的完整固件 |
| 2 | 传感器驱动 (7 个) | PPG/IMU/EDA/皮温/振动/电量计/充电 |
| 3 | VelaSense Engine | SQI、DSP、特征提取、基线、规则门控、TinyML |
| 4 | 腕上 LVGL 应用 | 实时状态、事件确认、趋势、呼吸训练、设置 |
| 5 | BLE GATT 服务 | 事件同步、配置、OTA |
| 6 | 手机网关协议 | 配对、摘要同步、Mimo 集成 |
| 7 | 测试报告 | DSP 单元测试、硬件在环、端到端隐私审计 |
| 8 | 技术文档 | BOM、接线图、构建说明、隐私说明 |
| 9 | 演示视频 | 端侧识别、腕上交互、断网可用 |
| 10 | 模型文件 | INT8 TinyML 模型 + 量化脚本 |

### 1.2 时间线总览 (8 周)

```
Week 1-2  ████████  BSP + 驱动 + 最小工程
Week 3    ████      信号处理 (SQI/滤波/峰值)
Week 4-5  ████████  算法模型 (特征/基线/TinyML)
Week 6    ████      产品功能 (UI/状态机/BLE/Mimo)
Week 7    ████      优化测试 (功耗/断连/稳定性)
Week 8    ████      交付演示 (文档/视频/代码)
```

---

## 二、模块架构

```
velasense/
├── docs/
│   └── specs/                    # 设计文档、BOM、接线图
├── firmware/
│   ├── boards/
│   │   └── velasense_board/      # 板级配置 (基于 sf32lb52_devkit_lcd)
│   │       ├── configs/nsh/defconfig
│   │       ├── src/
│   │       │   ├── velasense_bringup.c    # 板级初始化
│   │       │   ├── velasense_pinmux.c     # 引脚配置
│   │       │   └── velasense_power.c      # 电源管理
│   │       └── Kconfig
│   ├── apps/velasense/
│   │   ├── main.c                # 应用入口
│   │   ├── Kconfig
│   │   └── CMakeLists.txt
│   ├── drivers/                  # 传感器驱动
│   │   ├── max86141/             # PPG (SPI)
│   │   │   ├── max86141.c
│   │   │   ├── max86141.h
│   │   │   ├── max86141_uorb.c   # uORB 集成
│   │   │   └── Kconfig
│   │   ├── icm42688/             # IMU (SPI) — 或 bmi270
│   │   ├── ad5940/               # EDA (SPI)
│   │   ├── max30208/             # 皮温 (I2C)
│   │   ├── drv2605l/             # 振动 (I2C)
│   │   ├── max17048/             # 电量计 (I2C)
│   │   └── common/
│   │       ├── sensor_sync.c     # 统一时间戳
│   │       └── ring_buffer.c     # 环形缓冲区
│   ├── libs/                     # 算法库
│   │   ├── dsp/
│   │   │   ├── ppg_filter.c      # PPG 带通滤波
│   │   │   ├── ppg_sqi.c         # 信号质量指数
│   │   │   ├── motion_artifact.c # 运动伪影抑制
│   │   │   ├── peak_detect.c     # 搏动峰值检测
│   │   │   └── hrv.c             # HRV/RMSSD/SDNN
│   │   ├── features/
│   │   │   ├── feature_extract.c # 特征工程
│   │   │   ├── baseline.c        # 个人基线
│   │   │   └── activity.c        # 活动分类
│   │   ├── inference/
│   │   │   ├── tinyml.c          # INT8 推理引擎
│   │   │   ├── model_data.c      # 量化模型数据
│   │   │   └── rules_gate.c      # 规则门控
│   │   └── event/
│   │       ├── event_sm.c        # 事件状态机
│   │       ├── event_store.c     # 加密事件存储
│   │       └── event_ble.c       # BLE 同步
│   ├── ui/                       # LVGL 腕上应用
│   │   ├── screen_home.c         # 主页 (实时 HR + 状态)
│   │   ├── screen_event.c        # 事件确认
│   │   ├── screen_trend.c        # 趋势图表
│   │   ├── screen_breath.c       # 呼吸训练
│   │   ├── screen_settings.c     # 设置
│   │   └── ui_theme.c            # 主题与动画
│   ├── ble/                      # BLE 服务
│   │   ├── gatt_service.c        # GATT 服务定义
│   │   ├── ble_sync.c            # 摘要同步
│   │   └── ble_ota.c             # OTA 更新
│   └── tests/                    # 测试
│       ├── test_dsp.c            # DSP 单元测试
│       ├── test_features.c       # 特征提取测试
│       ├── test_event_sm.c       # 状态机测试
│       ├── test_privacy.c        # 隐私审计
│       └── test_integration.c    # 集成测试
├── tools/
│   ├── quantize_model.py         # 模型量化脚本
│   ├── simulate_sensors.py       # 传感器数据模拟器
│   ├── flash.sh                  # 烧录脚本
│   ├── serial_monitor.sh         # 串口监控脚本
│   └── generate_test_data.py     # 测试数据生成
├── data/
│   └── samples/                  # 脱敏测试样本 (仅 PPG 波形回放)
├── .env.example
└── .gitignore
```

---

## 三、详细开发计划

### Phase 1: BSP + 最小工程 (Week 1-2)

**目标**: 板卡启动、LCD、RTC、看门狗、BLE 广播、振动基础驱动

#### Week 1: 最小可运行固件

| 任务 | 输出 | 验收标准 |
|------|------|---------|
| 1.1 建立项目骨架 | 目录结构 + CMakeLists | 可 `cmake` 无错误 |
| 1.2 适配 defconfig | `velasense_board/configs/nsh/defconfig` | 基于 sf32lb52_devkit_lcd |
| 1.3 板级 bringup | `velasense_bringup.c` | NSH shell 可交互 |
| 1.4 LCD 最小显示 | LVGL 状态页 (版本+运行时间) | AMOLED 显示文字 |
| 1.5 RTC + 看门狗 | `date` + `wdog` 命令 | 时间正确、喂狗不复位 |
| 1.6 串口诊断框架 | 版本/启动原因/空闲内存输出 | 开机自动打印 |

#### Week 2: 外设驱动 + BLE 广播

| 任务 | 输出 | 验收标准 |
|------|------|---------|
| 2.1 振动马达驱动 | `drv2605l.c` (I2C) | `vibrate` 命令可触发 |
| 2.2 按键事件 | 短按/长按识别 | LCD 响应按键 |
| 2.3 BLE 初始化 | BT adapter + H4 驱动加载 | `bt` 命令可用 |
| 2.4 BLE 广播 | 设备名 "VelaSense-377" | 手机可扫描到 |
| 2.5 引脚核对文档 | 实际板卡引脚 vs 原理图 | 所有引脚确认无冲突 |
| 2.6 构建/烧录文档 | 完整步骤 + 截图 | 第三人可复现 |

**无硬件时可做**:
- ✅ 项目骨架搭建 (CMake/Kconfig)
- ✅ defconfig 编写
- ✅ 板级 bringup 代码框架
- ✅ LVGL UI 框架 (模拟器可预览)
- ✅ 构建验证 (编译通过即可)
- ✅ 振动驱动代码 (I2C 寄存器操作)
- ✅ BLE 服务框架代码

---

### Phase 2: PPG + IMU 传感器采集 (Week 2-3)

**目标**: PPG/IMU 驱动、统一时间戳、环形缓冲、串口诊断

#### 传感器驱动开发

| 驱动 | 接口 | 复杂度 | 预计工时 | 无硬件可做 |
|------|------|--------|---------|-----------|
| MAX86141 PPG | SPI | 中 | 3 天 | 寄存器定义 + 驱动框架 |
| ICM-42688-P IMU | SPI | 中 | 2 天 | 寄存器定义 + 驱动框架 |
| MAX30208 皮温 | I2C | 低 | 1 天 | 完整驱动 |
| DRV2605L 振动 | I2C | 低 | 1 天 | 完整驱动 |
| MAX17048 电量计 | I2C | 低 | 0.5 天 | 完整驱动 |
| AD5940 EDA | SPI | 高 | 5 天 | 寄存器定义 + 状态机框架 |

#### 数据管道

```
传感器硬件 → SPI/I2C 中断 → 驱动 ISR → 环形缓冲区 (RAM)
    → 统一时间戳 → 消息队列 → 算法任务
```

| 组件 | 说明 |
|------|------|
| `sensor_sync.c` | 基于硬件定时器的统一时间戳，误差 < 1ms |
| `ring_buffer.c` | 无锁环形缓冲，PPG 6000 样本 (60s@100Hz)，IMU 同上 |
| CSV 诊断输出 | 串口实时输出 PPG/IMU 数据，可导入 Python 分析 |

**验收标准**:
- PPG 原始波形正确 (红光/红外/绿光)
- IMU 6 轴数据正确
- 60 秒连续采集无丢帧
- CSV 可被 Python pandas 读取

**无硬件时可做**:
- ✅ MAX86141 寄存器定义 + SPI 驱动框架 (参考数据手册)
- ✅ ICM-42688-P 寄存器定义 + SPI 驱动框架
- ✅ MAX30208 完整 I2C 驱动
- ✅ MAX17048 完整 I2C 驱动
- ✅ 统一时间戳模块
- ✅ 环形缓冲区模块
- ✅ 传感器数据模拟器 (`tools/simulate_sensors.py`)

---

### Phase 3: 信号处理 (Week 3)

**目标**: SQI、带通滤波、搏动峰值、HR/PRV/RMSSD、运动抑制

#### DSP 模块

| 模块 | 输入 | 输出 | 算法 |
|------|------|------|------|
| `ppg_sqi.c` | 原始 PPG | SQI 0-1 | 基于幅值/频率/形态的多指标融合 |
| `ppg_filter.c` | 原始 PPG | 滤波后 PPG | 0.5-5Hz Butterworth 带通 |
| `motion_artifact.c` | PPG + IMU | 清洗后 PPG | 自适应噪声抵消 (ANC) |
| `peak_detect.c` | 滤波后 PPG | 搏动峰值序列 | 基于斜率/幅值的自适应阈值 |
| `hrv.c` | 搏动间期序列 | HR, RMSSD, SDNN | 标准时域 HRV 分析 |
| `activity.c` | IMU 数据 | 活动等级 (0-3) | 加速度幅值 + FFT 能量 |

#### 信号处理流水线

```
原始 PPG (100Hz)
  → SQI 门控 (< 0.70 → 跳过)
  → 带通滤波 (0.5-5Hz)
  → 运动伪影抑制 (IMU 参考)
  → 峰值检测
  → 异常搏动剔除 (>20% 偏离)
  → 搏动间期序列
  → HR / RMSSD / SDNN
```

**验收标准**:
- SQI 对噪声数据正确报警
- 滤波后信噪比提升 > 10dB
- 静息 HR MAE ≤ 3 bpm
- 有效搏动间期 ≥ 95%
- 运动排除召回 ≥ 90%

**无硬件时可做**:
- ✅ 全部 DSP 模块代码 (纯算法，不依赖硬件)
- ✅ Python 量化脚本
- ✅ 用公开 PPG 数据集验证 (如 MIMIC-III)
- ✅ 单元测试框架

---

### Phase 4: 算法模型 (Week 4-5)

**目标**: 特征工程、个人基线、规则门控、INT8 TinyML

#### Week 4: 特征工程 + 基线

| 任务 | 说明 |
|------|------|
| 特征提取 | HR 斜率、RMSSD、SDNN、搏动间期离散度、活动强度、姿态、SCL、SCR 次数/幅度、皮温斜率 |
| 个人基线 | 24h 滚动历史，按时段/活动/佩戴分组，指数滑动更新 |
| 规则门控 | 排除: 高强度运动、SQI < 0.70、未佩戴、快速环境变化 |

#### Week 5: TinyML 推理

| 任务 | 说明 |
|------|------|
| 模型选择 | 时序分类模型 (LSTM/TCN/Transformer-lite) |
| 量化 | FP32 → INT8 (TensorFlow Lite Micro 或自研) |
| 融合策略 | 晚期特征融合: 各模态特征 → 拼接 → 分类 |
| 输出 | 自主神经唤醒概率 + 原因码 (心动/紧张/惊喜/压力/其他) |
| 阈值 | 连续 15s 置信度 > 0.82 → 候选事件 |
| 冷却 | 同类事件 5 分钟冷却 |

**验收标准**:
- 推理时间 < 100ms (SF32LB52 Cortex-M33)
- 个体基线学习后 F1 ≥ 0.80
- 静坐 8h 误提醒 ≤ 1 次

**无硬件时可做**:
- ✅ 特征提取模块 (纯算法)
- ✅ 个人基线模块
- ✅ 规则门控模块
- ✅ 模型训练 (Python, 用公开数据集)
- ✅ 量化脚本 (`tools/quantize_model.py`)
- ✅ 模型转 C 数组 (`model_data.c`)
- ✅ 推理引擎框架 (`tinyml.c`)
- ✅ 模拟器上验证推理逻辑

---

### Phase 5: 产品功能 (Week 6)

**目标**: 腕上 UI、事件状态机、BLE 同步、Mimo 集成

#### 5.1 腕上 LVGL 应用

| 界面 | 功能 | 交互 |
|------|------|------|
| 主页 | 实时 HR + 活动状态 + SQI 指示 | 自动刷新 |
| 事件确认 | 振动提醒 → 5 选 1 标签 | 滑动选择 |
| 趋势 | 24h HR 趋势 + 事件时间线 | 左右滑动 |
| 呼吸训练 | 4-4-6 节拍 + 振动引导 | 开始/停止 |
| 设置 | 蓝牙/隐私/通知/关于 | 菜单导航 |

#### 5.2 事件状态机

```
IDLE → MONITORING → CANDIDATE → ALERTING → CONFIRMED/REJECTED
         ↑                              |
         └──────── COOLDOWN ←───────────┘
```

| 状态 | 条件 | 动作 |
|------|------|------|
| IDLE | 未佩戴/SQI 低 | 不检测 |
| MONITORING | 正常佩戴 | 持续推理 |
| CANDIDATE | 连续 15s > 0.82 | 进入候选 |
| ALERTING | 候选确认 | 振动 + LCD 提醒 |
| CONFIRMED | 用户选择标签 | 存储事件 |
| REJECTED | 用户标记误报 | 更新阈值 |
| COOLDOWN | 事件后 5 分钟 | 暂停检测 |

#### 5.3 BLE GATT 服务

| 特征 | UUID | 方向 | 说明 |
|------|------|------|------|
| Event Notify | 自定义 | Notify | 实时事件推送 |
| Event Summary | 自定义 | Read | 历史摘要列表 |
| User Label | 自定义 | Write | 手机端确认标签 |
| Config | 自定义 | Read/Write | 阈值/采样率配置 |
| OTA Data | 自定义 | Write | 固件更新 |

#### 5.4 Mimo Token Plan 集成

```
手机网关 (HTTPS)
  → Mimo API (结构化摘要)
  → 情绪日记 / 周报 / 呼吸建议
  → 返回腕表显示
```

**输入格式** (脱敏):
```json
{
  "events": [
    {"time": "2026-08-11T14:30:00", "label": "pressure", "confidence": 0.87, "activity": "sitting"},
    {"time": "2026-08-11T15:45:00", "label": "surprise", "confidence": 0.91, "activity": "walking"}
  ],
  "hr_trend": "stable",
  "stress_level": "moderate",
  "user_note": "今天开会比较多"
}
```

**隐私约束**:
- ❌ 不上传原始 PPG/EDA 波形
- ❌ 不做医疗诊断
- ❌ 不推断恋爱关系
- ✅ 仅上传用户确认后的结构化摘要
- ✅ 密钥仅从 `.env` 读取，不入代码/日志

**无硬件时可做**:
- ✅ LVGL 全部界面 (模拟器预览)
- ✅ 事件状态机代码 + 单元测试
- ✅ BLE GATT 服务定义
- ✅ Mimo API 对接代码
- ✅ 手机网关协议文档

---

### Phase 6: 优化测试 (Week 7)

**目标**: 功耗、断连、降级、隐私、72h 稳定性

| 测试项 | 方法 | 判定标准 |
|--------|------|---------|
| 功耗优化 | 关闭未用外设、降低采样率、休眠 | 续航 ≥ 12h (开发板) |
| BLE 断连补传 | 模拟断连 → 重连 → 补传 | 数据不丢失 |
| 低电量降级 | 电量 < 20% → 降低采样/关动画 | 正常降级不崩溃 |
| 模型回滚 | OTA 新模型失败 → 回退旧版 | 自动回退成功 |
| 数据清理 | 用户清空历史 | Flash 数据不可恢复 |
| 隐私审计 | grep 检查 + 抓包 | 无原始波形上传 |
| 72h 压力测试 | 连续运行 + 模拟事件 | 无死机/无损坏 |

**无硬件时可做**:
- ✅ 隐私审计脚本
- ✅ 断连补传逻辑代码
- ✅ 模型回滚代码
- ✅ 数据清理代码
- ✅ 自动化测试框架

---

### Phase 7: 交付 (Week 8)

| 交付物 | 格式 |
|--------|------|
| 固件 | `nuttx.bin` + 烧录脚本 |
| 源代码 | Git 仓库 + 分支策略 |
| BOM | CSV/Excel |
| 接线图 | 原理图 PDF + 文字说明 |
| 模型文件 | `.tflite` + C 数组 + 量化脚本 |
| 测试报告 | PDF |
| 隐私说明 | Markdown |
| 演示视频 | MP4 (3-5 分钟) |
| README | 作品说明 (替换模板) |

---

## 四、风险与缓解

| # | 风险 | 概率 | 影响 | 缓解 |
|---|------|------|------|------|
| R1 | 全部传感器驱动需从零编写 | 确定 | 高 | 优先 PPG+IMU，EDA 降级 |
| R2 | ICM-42688-P 缺货 | 中 | 中 | 改用 BMI270 (有驱动) |
| R3 | AD5940 EDA 驱动复杂 | 高 | 中 | 降级为 PPG+IMU 方案 |
| R4 | GPIO 引脚不足 | 低 | 高 | I2C 共享总线，SPI 多 CS |
| R5 | TinyML 推理超时 | 中 | 高 | 简化模型/减少特征数 |
| R6 | BLE 双核 IPC 问题 | 低 | 中 | 已有成熟 adapter 代码 |
| R7 | 板子到货延迟 | 中 | 高 | 先做纯软件开发 (DSP/TinyML/UI) |
| R8 | 比赛截止时间紧 | 中 | 高 | 分阶段交付，核心功能优先 |

---

## 五、无硬件开发策略

板子到货前，以下模块可以 **100% 完成**：

| 模块 | 方法 | 完成度 |
|------|------|--------|
| 项目骨架 | CMake/Kconfig/目录结构 | 100% |
| defconfig | 基于 sf32lb52_devkit_lcd | 100% |
| DSP 算法 | 纯 C，用公开数据集验证 | 100% |
| 特征提取 | 纯 C | 100% |
| 个人基线 | 纯 C | 100% |
| 规则门控 | 纯 C | 100% |
| TinyML 模型 | Python 训练 + 量化 + C 导出 | 100% |
| 推理引擎 | 纯 C | 100% |
| 事件状态机 | 纯 C + 单元测试 | 100% |
| LVGL UI | 模拟器预览 | 90% |
| BLE GATT | 框架代码 | 80% |
| Mimo API | 手机端代码 | 80% |
| I2C 传感器驱动 | MAX30208/DRV2605L/MAX17048 | 100% |
| SPI 传感器驱动 | MAX86141/ICM42688/AD5940 框架 | 70% (需硬件验证) |
| 隐私审计脚本 | Python | 100% |
| 测试框架 | CMocka + 自动化 | 100% |

**预计无硬件可完成**: 约 70-80% 的代码量。板子到货后主要做硬件集成、引脚验证、传感器调试和端到端测试。

---

## 六、技术栈

| 层级 | 技术 |
|------|------|
| OS | openvela (NuttX) |
| 语言 | C (固件), Python (工具/训练) |
| 构建 | CMake + Ninja |
| UI | LVGL 8.x |
| 传感器框架 | uORB |
| BLE | NuttX BT stack (H4 + GATT) |
| ML 训练 | PyTorch / TensorFlow |
| ML 部署 | INT8 量化 → C 数组 |
| 测试 | CMocka + pytest |
| 版本控制 | Git + GitHub PR |
| CI | GitHub Actions (编译检查) |

---

## 七、每日站会节奏

| 时间 | 内容 |
|------|------|
| 每天 10:00 | 昨日完成 / 今日计划 / 阻塞项 |
| 每周五 | 周总结 + 下周计划 + 风险更新 |
| 里程碑 | 每阶段结束验收 + demo |

---

## 八、立即可开始的任务

1. 搭建项目骨架 (`velasense/` 目录结构)
2. 编写 defconfig (基于 sf32lb52_devkit_lcd)
3. 开发 DSP 算法模块 (纯 C，不依赖硬件)
4. 训练 TinyML 模型 (Python，用公开 PPG 数据集)
5. 编写 I2C 传感器驱动 (MAX30208/DRV2605L/MAX17048)
6. 开发 LVGL UI (模拟器预览)
7. 编写事件状态机 + 单元测试
8. 编写隐私审计脚本
