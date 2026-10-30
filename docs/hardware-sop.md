# WIH 双板同步与 Pogo 验收 SOP

本流程用于把同一固件安全同步到两台电脑控制的 N8R8，并验证持续监听、即时传词和句子互动。任何一步失败都先记录原始日志，不把 native 或 Web 模拟结果当作实板通过。

## 1. 固定硬件与身份

- 两块板均为 ESP32-S3-DevKitC-1 N8R8，只使用 `esp32-s3-devkitc-1-n8r8` 固件环境。
- 第一板节点 ID：`0000ac3a2a6b4580`，对应 `WIH_MEMBER_1_ID`。
- 第二板节点 ID：`0000683d2a6b4580`，对应 `WIH_MEMBER_2_ID`。
- 两板的 `WIH_KIT_ID`、`WIH_RECORDER_ID` 和完整 member 表必须一致。启动时双方都应出现 `configured=1 uart=1 identity=1` 和 `psram: found=1`。

## 2. 持续监听要求

无操作约 5 秒后只允许墨水屏 hibernate，MCU 不得进入 deep sleep。UART、握手、`game.tick()`、OFFER/PULL 重试和 `alive` 日志始终运行。屏幕已休眠且无绘制变化时不得反复写 NVS。

Pogo 连接、有效来词和本地输入应立即处理；显示恢复时重新初始化并全刷，不需要按 A 唤醒。传输不等待屏幕恢复。

## 3. Pogo 断电复核

当前固定拓扑为第一板 A.R ↔ 第二板 B.L：

| 信号 | 第一板 A.R（UART2） | 方向 | 第二板 B.L（UART1） |
|---|---|---|---|
| 第一板发送 / 第二板接收 | GPIO15（TX） | → | GPIO18（RX） |
| 第二板发送 / 第一板接收 | GPIO16（RX） | ← | GPIO17（TX） |
| 公共参考地 | GND | ↔ | GND |

第一板 IO15 到第二板 IO18 只覆盖一个方向，仍须复核第二板 IO17 到第一板 IO16 和 GND。不要连接两板电源正极，不要把 TX 接 TX 或 RX 接 RX。电气热插拔风险仍以 `docs/wiring.md` 为准。

## 4. 修改、测试与发布顺序

1. 修改代码及对应文档，运行 `git diff --check`。
2. 运行 46 项 native 测试：`pio test -e native`。
3. 运行 14 项 Web 测试：`python3 -m unittest simulator.test_simulator -v`。
4. 构建 N8R8 固件并烧录本机；PlatformIO 卡在包管理时按 `AGENTS.md` 的已验证本机 SCons 方法处理。
5. 本机串口验收通过后再提交、推送并创建 PR，不推送未实板验证的休眠或通信改动。
6. 另一台电脑同步同一分支和提交，核对 `git log -1 --oneline`，再按其实际串口构建烧录。
7. 两块板配置和启动日志均正确后连接 Pogo，执行第 5 节验收。

## 5. 实板验收

### 5.1 显示休眠后 MCU 持续运行

1. 保持无操作超过 10 秒。
2. 应看到一次 `display: hibernate; MCU/UART awake`，墨水屏画面保持。
3. 之后必须继续周期性出现 `alive ...`，不能在面板 `_PowerOff` 后停止日志或进入任何 MCU 休眠流程。
4. 屏幕休眠且无变化时不得持续写 NVS 或重复刷新。

### 5.2 Pogo 即时握手

1. 两板均已完成启动；显示可以处于 hibernate，MCU 必须仍在输出 `alive`。
2. 接好 Pogo 后双方 `links` 应由 0 变为 1，不需要按 A。
3. 若失败，记录双方 `wlp2:`、`alive`、`uartErr` 和实际接线，先区分身份、单向接线和握手问题。

### 5.3 即时传词与句子互动

1. 第一板在 Picker 按两次 A，应立即 OFFER；第二板立即进入 Sentence/Navigate，不需要额外推进。
2. 第一板只有收到 ACCEPT 后才换词，第二板不能重复插入同一 req。
3. 编码器循环移动 cursor；无连接时 A→A 删除，B 取消。
4. 唯一邻居时 A 进入 InsertSide，B 选择 BEFORE/AFTER，再 A 按 cursor 前/后位置 pull。
5. 断线重连不重复插入、不清空句子；显示恢复后的第一次重绘必须全刷。

## 6. 另一台电脑最简同步要求

同步同一分支后检查提交 ID，运行 native 测试，按实际串口烧录 N8R8，并保留两板一致的 `src/config.h`。不要用 Web 模拟器代替实板，不要修改即时发送和 pending 协议。完成后返回烧录提交、`wlp2:`、`alive ... links=...` 和传词验收日志。
