# WIH-Jiaoyang

WIH 是基于 ESP32-S3、1.54 英寸墨水屏和双 UART 邻接连接的协作随机选词器。多块板可以传递当前候选词，并在接收板上编辑一条最多 12 词的有序句子。

此网页连接版基于 `WIH-Jiaoyang-main (1).zip`，新增 **USB 串口 → Words in Hand 网页** 的单向选词事件，不替换原有 WLP2 双板协议。连接后，在 Picker 页第一次按 A 确认会向网页追加一个词块；第二次 A 仍用于板间传词，不重复追加。安装、操作、边界和验收见 **[网页连接说明](docs/web-connection.md)**。本次只修改源码并测试、编译，未烧录或实板联调。

## 硬件与固件

- **开发板**：ESP32-S3-DevKitC-1，全板固定 **N8R8：8 MB QIO Flash + 8 MB OPI PSRAM**。
- **外设**：Waveshare 1.54" e-Paper V2/V2.1（GDEH0154D67，200×200）、EC11 旋转编码器、A/B 两个实体按钮。
- **框架**：PlatformIO + Arduino；依赖 GxEPD2、ESP32Encoder。
- **唯一固件环境**：`esp32-s3-devkitc-1-n8r8`，同时是 `default_envs`。构建和烧录不需要 `-e`：

```sh
pio run
# 仅在自行确认板型、端口并准备烧录后执行：
pio run -t upload
```

`pio upload` 同样使用默认环境。上述默认构建/烧录命令只针对 N8R8，不生成其他 Flash/PSRAM 版本；`native` 仅用于测试。词库更新运行 `python3 tools/build_words.py`。节点不再需要预注册 member ID；相同 `WIH_KIT_ID` 的物理邻居开放加入，ID 冲突时自动换号并保存到 NVS，详见 [开放加入与冲突恢复](docs/open-join.md)。

## 当前交互

- **Picker**：内置 2000 个高频英语词，编码器双向无终点选词。A 第一次确认，第二次主动向唯一直接邻居 offer；无邻居或有两个邻居时不自动选目标。B 刷新或取消确认/pending。
- **Sentence**：任何收词都会进入句子页，最多 12 个有序词。无连接时编码器循环高亮，A 弹出 `delete? y/n`，再按 A 删除、B 取消；Navigate 下 B 清句并回 Picker。
- **定位拉取**：有唯一有效邻居时，A 进入 InsertSide，显示对方当前候选词；B 切换 `L BEFORE` / `R AFTER`，A 发 `WORD_PULL` 按所选词的前/后位置拉取。空 Sentence 也能拉取首词。
- **位置规则**：定位 pull 只按句内 `pos`，与物理 UART 左右无关。无 `pos` 的主动 offer 仍按接收入边处理：Left 到句首、Right 到句尾。
- **完成与故障恢复**：接收方即句子块；发送方仅收到应用层 `WORD_ACCEPT` 后才随机新词。offer/pull 在运行中断线时保留 req 重试且不重复插入，B 可取消本机 pending，拔线不清句。

完整的模式切换、双机例子、按键上下文、屏幕布局、协议时序、热插拔和验收步骤见 **[docs/sentence-editor.md](docs/sentence-editor.md)**。

## Web 双板模拟器

仅用 Python 3 标准库即可启动，无需安装前端依赖：

```sh
python3 simulator/server.py
```

浏览器打开 `http://127.0.0.1:8000/`；可用 `--port` 换端口。页面支持设置 A/B 候选词、连接/断开 A.R↔B.L、编码器与 A/B 按钮、空闲时自动完成 offer/pull、异常 pending 重试、删除、清句和日志。完整操作与 API 见 **[docs/simulator.md](docs/simulator.md)**。

模拟器是确定性交互模型，不执行真实 WLP2 帧，也不验证固件或真实 UART。它允许中文 UTF-8 输入，但真实固件仍只有 ASCII 显示保证。

## 显示、通信与保存

- **显示与持续监听**：三行选词页和 200×200 句子页；句子按顺序换行，cursor 词反显，长词缩略。无操作约 5 秒后只让墨水屏 hibernate，MCU 不进入深睡，UART、握手、`game.tick()` 和重试始终运行；连接、来词或本地输入会立即重新初始化并全刷显示，无需 A 唤醒。
- **通信**：WLP2 v3 双 UART 开放握手、kit 校验、MAC 设备指纹、ID 冲突自动换号、sid 心跳、CRC32、LACK/LBUSY、重传去重、NODE_INFO 邻接核对和最多三块短链转发。词传输要求直接邻居，不需要 member 白名单。
- **持久化**：WIH4 保存 Picker、句子顺序、cursor、来源和 receipts，并迁移 NVS WIH1/WIH2/WIH3；`wih/node_id` 保存冲突后分配的新节点 ID。编辑模式与 offer/pull pending **不保证跨断电恢复**；断线重试不等于断电续传。

## 测试与验收

```sh
pio test -e native
python3 -m unittest simulator.test_simulator -v
```

当前共 **58 项 C++ 核心测试**（原 49 项 + 9 项网页事件测试）和 **14 项模拟器测试**。核心测试覆盖 JSON/WLP2、开放加入、同设备重启、ID 冲突恢复、故障注入、双机/三块、主动 offer、定位 pull、删除/cursor、容量、持久化恢复、幂等、热插拔，以及首次确认事件、取消后重确认、会话重启和周期 ready；模拟器测试覆盖确定性模型与 HTTP API。网页目录另外提供 **20 项 JavaScript 串口测试**与 **7 项隔离 HTTP 测试**。

本次 Windows 环境缺少 gcc/g++，`pio test -e native` 未能直接执行；58 项 C++ 测试由 MSVC 编译同一组源码并全部通过，N8R8 的 `pio run` 也已通过。完整验证结果及中文路径构建处理见 [网页连接说明](docs/web-connection.md#软件验证结果)。测试或目标编译通过都不代表实板已验证。

## 文档与边界

- [网页连接、协议与实板验收](docs/web-connection.md)
- [开放加入与节点 ID 冲突恢复](docs/open-join.md)
- [句子编辑器详细说明与手动验收](docs/sentence-editor.md)
- [双板 Web 模拟器使用说明](docs/simulator.md)
- [显示设计需求与验收规范](docs/display-design-requirements.md)
- [接线、电气风险与按钮极性](docs/wiring.md)
- [双板同步与 Pogo 验收 SOP](docs/hardware-sop.md)
- [通信、休眠与双板排查手册](docs/troubleshooting.md)

当前字体只保证 ASCII 显示；开放加入不提供密码学认证；CAPTURE/READY 记录者冻结保存事务、句子历史 Flash 日志和电脑底座导出尚未实现。实体屏幕、真实 UART、电气安全热插拔、持续监听功耗和残影仍须按文档手动验收。
