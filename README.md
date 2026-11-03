# WIH-Jiaoyang

WIH 是基于 ESP32-S3、1.54 英寸墨水屏和双 UART 邻接连接的协作随机选词器。多块板可以传递当前候选词，并在接收板上编辑一条最多 12 词的有序句子；单机模式也可以直接用本机词库造句。

## 硬件与固件

- **开发板**：ESP32-S3-DevKitC-1，全板固定 **N8R8：8 MB QIO Flash + 8 MB OPI PSRAM**。
- **外设**：Waveshare 1.54" e-Paper V2/V2.1（GDEH0154D67，200×200）、EC11 旋转编码器、A/B 两个实体按钮。
- **框架**：PlatformIO + Arduino；依赖 GxEPD2、ESP32Encoder。
- **唯一固件环境**：`esp32-s3-devkitc-1-n8r8`，同时是 `default_envs`。构建和烧录不需要 `-e`：

```sh
pio run
pio run -t upload
```

`pio upload` 同样使用默认环境。上述默认构建/烧录命令只针对 N8R8；`native` 仅用于测试。词库更新运行 `python3 tools/build_words.py`。节点无需预注册 member ID；相同 `WIH_KIT_ID` 的物理邻居开放加入，ID 冲突时自动换号并保存到 NVS，详见 [开放加入与冲突恢复](docs/open-join.md)。

## 当前交互

- **Picker**：编码器双向选词。A 第一次选中，第二次向唯一直接邻居发送；B 在已选中时取消，未选中时 shuffle。长按 A 弹出单机模式确认页。
- **单机模式**：确认后进入 Sentence 插入光标模式。编码器按键或 A+B 仍可切换 Picker/Sentence；单机 Picker 的第二次 A 改为把本机当前词插到保存的竖线光标处。Picker 再次长按 A 可确认退出单机模式。
- **Sentence 选择模式**：当前词带下划线，编码器循环移动选择 cursor。短按 A 进入插入模式；短按 B 删除当前词，长按 B 清空整句，两者都有确认页。
- **Sentence 插入模式**：显示竖线插入光标，编码器在句首、词间和句尾移动位置。短按 A 返回选择模式，B 取消插入模式。普通来词在此模式下插到竖线处；选择模式下仍按物理 Left/Right 放到句首/句尾。带 `pos` 的定位消息始终按指定位置插入。
- **上传电脑**：Sentence 非空时长按 A 弹出上传确认页。所有确认页均为 **A 确认、B 取消**。
- **完成与故障恢复**：发送方仅在收到应用层 `WORD_ACCEPT` 后随机新词；运行中断线会保留 req 重试且不重复插入，B 可取消本机 pending，拔线不清句。

完整状态机、协议、UART 格式和验收步骤见 **[docs/sentence-editor.md](docs/sentence-editor.md)**。

## USB-to-UART 与网页

电脑连接采用开发板 **USB-to-UART 口（UART0，115200 baud）**，不占用 Pogo 的 Serial1/Serial2。原有连接与上传逻辑不变：Picker 第一次 A 输出 `word_confirmed` 单词事件，网页「放上整个句子」仍通过 `get_sentence` 读取整句。

Sentence 长按 A 仅新增一条并行上传路径：确认后按 `upload_begin` / `upload_word` / `upload_end` 分帧发送冻结快照，网页完整接收后追加，不改变原单词事件、连接或按钮读取流程。使用说明见 **[WIH-new-2.0/README-web-connect.md](WIH-new-2.0/README-web-connect.md)**。

## 显示、通信与保存

- **显示与持续监听**：Picker 三行选词；Sentence 选择态为下划线、插入态为竖线。无操作约 5 秒后只让墨水屏 hibernate，MCU 不深睡，UART、握手、`game.tick()` 和重试始终运行。
- **通信**：WLP2 v3 双 UART 开放握手、kit 校验、MAC 指纹、ID 冲突自动换号、sid 心跳、CRC32、LACK/LBUSY、重传去重和 NODE_INFO 邻接核对。词传输要求直接邻居。
- **持久化**：WIH5 保存 Picker、句子顺序、选择 cursor、竖线位置、单机标记、来源和 receipts，并迁移 WIH1/WIH2/WIH3/WIH4。编辑确认页与 offer/pull pending 不保证跨断电恢复；断线重试不等于断电续传。

## 测试与验收

```sh
pio test -e native
node --test WIH-new-2.0/web/tests/serial-bridge.test.mjs
pio run
```

当前共 **60 项 native 测试**，覆盖 JSON/WLP2、开放加入、故障恢复、Picker/Sentence、竖线定位、单机模式、删除/清句确认、UART 握手、整句快照和上传分帧。网页串口桥另有 Node 测试。测试或目标编译通过都不代表实板、电气或真实 UART 已验证。Web 双板模拟器已移除。

## 文档与边界

- [开放加入与节点 ID 冲突恢复](docs/open-join.md)
- [句子编辑器、上传协议与手动验收](docs/sentence-editor.md)
- [显示设计需求与验收规范](docs/display-design-requirements.md)
- [接线、电气风险与按钮极性](docs/wiring.md)
- [双板同步与 Pogo 验收 SOP](docs/hardware-sop.md)
- [通信、休眠与双板排查手册](docs/troubleshooting.md)

当前字体只保证 ASCII 显示；开放加入不提供密码学认证；CAPTURE/READY 记录者冻结保存事务和句子历史 Flash 日志尚未实现。实体屏幕、真实 UART、电气安全热插拔、持续监听功耗和残影仍须按文档手动验收。
