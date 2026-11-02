# WIH 双板同步与 Pogo 验收 SOP

本流程用于把同一固件安全同步到两台电脑控制的 N8R8，并验证持续监听、即时传词、竖线插入和电脑上传。任何一步失败都先记录原始日志，不把 native 或网页模拟结果当作实板通过。日志排查见 [通信、休眠与双板排查手册](troubleshooting.md)。

## 1. 固定硬件与开放身份

- 两块板均为 ESP32-S3-DevKitC-1 N8R8，只使用 `esp32-s3-devkitc-1-n8r8` 固件环境。
- 不配置 member/recorder 白名单；节点 ID 首次由 MAC 派生，相同 `WIH_KIT_ID` 即可开放加入。
- 同设备重启不换号；同 ID 不同指纹时双方随机换号并保存到 NVS。启动双方都应出现 `configured=1 uart=1 identity=1` 和 `psram: found=1`。

## 2. 持续监听要求

无操作约 5 秒后只允许墨水屏 hibernate，MCU 不得进入 deep sleep。UART、握手、`game.tick()`、OFFER/PULL 重试和 `alive` 日志始终运行。屏幕休眠且无变化时不得反复写 NVS。

Pogo 连接、有效来词和本地输入应立即处理；显示恢复时重新初始化并全刷，不需要按 A 唤醒。传输不等待屏幕恢复。

## 3. Pogo 断电复核

当前固定拓扑为第一板 A.R ↔ 第二板 B.L：

| 信号 | 第一板 A.R（UART2） | 方向 | 第二板 B.L（UART1） |
|---|---|---|---|
| 第一板发送 / 第二板接收 | GPIO15（TX） | → | GPIO18（RX） |
| 第二板发送 / 第一板接收 | GPIO16（RX） | ← | GPIO17（TX） |
| 公共参考地 | GND | ↔ | GND |

须复核两个方向和 GND。不要连接两板电源正极，不要把 TX 接 TX 或 RX 接 RX。电气热插拔风险仍以 `docs/wiring.md` 为准。

## 4. 修改、测试与发布顺序

1. 修改代码及对应文档，运行 `git diff --check`。
2. 运行 60 项 native 测试：`pio test -e native`。
3. 运行网页串口桥测试：`node --test WIH-new-2.0/web/tests/serial-bridge.test.mjs`。
4. 构建 N8R8 固件；PlatformIO 卡在包管理时按 `AGENTS.md` 的已验证本机 SCons 方法处理。只有用户明确要求时才烧录。
5. 本机串口验收通过后再提交、推送并创建 PR，不推送未实板验证的休眠或通信改动。
6. 另一台电脑同步同一分支和提交，核对 `git log -1 --oneline`，再按其实际串口构建烧录。
7. 两块板配置和启动日志均正确后连接 Pogo，执行下节验收。

## 5. 实板验收

### 5.1 显示休眠后 MCU 持续运行

1. 保持无操作超过 10 秒。
2. 应看到一次 `display: hibernate; MCU/UART awake`，墨水屏画面保持。
3. 之后必须继续周期性出现 `alive ...`，不能停止日志或进入 MCU 休眠。
4. 屏幕休眠且无变化时不得持续写 NVS 或重复刷新。

### 5.2 Pogo 即时握手

1. 两板均已完成启动；显示可以 hibernate，MCU 必须仍输出 `alive`。
2. 接好 Pogo 后双方 `links` 应由 0 变为 1，不需要按 A。
3. 若失败，记录双方 `wlp2:`、`alive`、`uartErr` 和实际接线，区分身份、单向接线和握手问题。

### 5.3 传词、竖线和句子互动

1. 第一板 Picker 按两次 A 应立即 OFFER；第二板进入 Sentence，不需要额外推进。
2. 第一板只有收到 ACCEPT 后才换词，第二板不能重复插入同一 req。
3. Navigate 中下划线循环移动；短 B 弹删除确认，A 删除、B 取消。长 B 弹清句确认。
4. A 进入竖线插入模式，旋转移动句首/词间/句尾位置；普通来词落在竖线处。退出插入模式后，Left/Right 普通来词仍分别落在句首/句尾。
5. 断线重连不重复插入、不清句；显示恢复后的第一次重绘必须全刷。

### 5.4 单机与电脑上传

1. Picker 长按 A 并确认进入单机，切换 Picker/Sentence，第二次 A 应插入本机当前词而不是 Pogo 发送。
2. 非空 Sentence 长按 A，B 应取消上传；再次进入并 A 确认。
3. 原单词上传和按钮读取整句保持不变；网页连接后，长按 A 应收到完整 `upload_begin/upload_word/upload_end`，且只追加一次整句。
4. 面板休眠后重试连接、收词和上传，确认 MCU/UART 未停止。

## 6. 另一台电脑最简同步要求

同步同一分支后检查提交 ID，运行 native 和网页串口测试，按实际串口烧录 N8R8，并保留两板一致的 `src/config.h`。完成后返回烧录提交、`wlp2:`、`alive ... links=...`、传词和上传验收日志。不要修改即时发送、pending 幂等协议或把软件测试写成实板通过。
