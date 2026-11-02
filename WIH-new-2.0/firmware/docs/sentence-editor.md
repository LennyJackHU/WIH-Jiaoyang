# WIH 句子编辑器：交互、上传、协议与验收

本文对应当前 `esp32-s3-devkitc-1-n8r8` 固件。实现位置为 `src/input.cpp`、`src/word_game.cpp`、`src/sentence.cpp`、`src/display.cpp`、`src/web_serial.cpp`、`src/storage.cpp` 及 native 测试。软件测试不等于实板、电气或真实 UART 验收。

## 1. 核心模型

- Picker 始终有一个当前候选词；句子是最多 12 个词的有序数组，每项保存文本和来源 ID。
- Sentence 有两种普通状态：
  - **选择模式 `Navigate`**：下划线标记当前词，编码器循环移动选择 cursor。
  - **插入模式 `InsertCursor`**：竖线表示句首、词间或句尾的插入位置，编码器移动该位置，范围限制在 `0..count`。
- 没有插入光标时，无 `pos` 来词按物理入边处理：Left 到句首，Right 到句尾。
- 插入光标模式下，无 `pos` 来词插入竖线位置，并把竖线移到新词之后。带 `pos` 的消息始终严格按消息位置插入，与物理 UART 左右无关。
- 同一 `src + boot + req` 的重试只插入一次。无 `pos` 请求首次在竖线处接收后，即使接收方退出插入模式，后续重试仍按原 receipt 幂等处理，不会重复插入或重新解释位置。
- 发送方只有收到应用层 `WORD_ACCEPT` 才取消确认并随机新词；WLP2 链路层 LACK 不等于已入句。

## 2. 按键总表

短按 A/B 在松开时派发；长按阈值约 800ms。所有确认页都是 **A 确认、B 取消**，确认页中编码器不改变目标。

| 当前上下文 | 编码器 | 短按 A | 短按 B | 长按 A | 长按 B |
|---|---|---|---|---|---|
| Picker，未选中 | 浏览候选 | 选中当前词 | shuffle | 单机模式确认 | 无动作 |
| Picker，已选中 | 取消选中并换词 | 联机：发送；单机：本机入句 | 取消选中 | 单机模式确认 | 无动作 |
| Sentence / Navigate | 循环选择词 | 进入 InsertCursor，竖线默认在所选词后 | 删除当前词确认 | 上传整句确认 | 清空整句确认 |
| Sentence / InsertCursor | 移动竖线 | 返回 Navigate | 取消并回 Navigate | 上传整句确认 | 清空整句确认 |
| 单机/上传/删除/清句确认 | 忽略 | 确认 | 取消 | 无动作 | 无动作 |

其他保留操作：

- 编码器按键或 A+B 在 Picker/Sentence 间切换；确认页和传输 pending 时不切换。
- offer/pull pending 时 A 不启动另一笔操作；B 优先取消本机 pending。取消不是分布式撤销，对端已入句内容不会远程删除。
- 空 Sentence 长按 A 显示 `NO SENTENCE`，不进入上传确认。

## 3. 单机模式

1. 在 Picker 长按 A，出现 `ENTER SOLO?`；A 进入，B 取消。
2. 进入后显示 Sentence 插入光标。用编码器按键或 A+B 返回 Picker。
3. 单机 Picker 中 A 第一次选中，第二次 A 不再发 Pogo offer，而是把本机当前词插入保存的竖线位置。
4. 本机入句后回到 Sentence 插入模式，竖线移到新词后，Picker 随机出新候选。继续用编码器按键或 A+B 切换页面，可连续造句。
5. 单机 Picker 再次长按 A 显示 `EXIT SOLO?`，确认后退出单机模式；已有句子保留。
6. 单机标记和竖线位置随 WIH5 保存。重启后恢复标记和位置，但编辑会话回 Navigate。

## 4. 删除与清句

- Navigate 短按 B：对下划线所在词弹出 `DELETE THIS PIECE?`。A 删除，B 取消。
- 删除后选择 cursor 指向原位置的后一个词；若删除末尾词，则指向新的末尾词。删除唯一词后仍留在空 Sentence。
- 非空 Sentence 长按 B：弹出 `CLEAR COLLAGE?`。A 清空并回 Picker，B 取消。
- InsertCursor 的短按 B 只取消插入模式，不删除词。

## 5. USB-to-UART 整句上传

### 5.1 原有连接与读取逻辑

使用开发板 **USB-to-UART 接口对应的 UART0，`115200 baud`**。Pogo 邻接通信继续使用 Serial1/Serial2，互不占用。原有网页连接流程不变：

- Picker 第一次 A 输出一次 `word_confirmed`，第二次 A 不重复输出。
- 网页「放上整个句子」发送 `get_sentence`，固件仍按 `sentence_begin/sentence_word/sentence_end` 返回冻结快照。
- 长按 A 主动上传是独立新增路径，不增加 `hello`，也不改变以上连接、单词事件和按钮读取行为。

### 5.2 长按 A 操作与快照

1. 非空 Sentence 长按 A，出现 `UPLOAD TO COMPUTER?`。
2. A 确认后冻结当时句子，按顺序分帧发送；B 取消则不发送。
3. 若另一份句子快照正在发送，显示 `UPLOAD BUSY`，不并发交错两批分帧。
4. 屏幕 `UPLOADING` 只表示快照已开始写入 UART，不是电脑端应用层接收回执。当前上传协议没有离线补发队列或网页 ACK。

格式为一行一个 `WIH:` 前缀的 JSON：

```text
WIH:{"type":"upload_begin","protocol":1,"device":"...","session":"...","request":"...","count":2}
WIH:{"type":"upload_word","protocol":1,"device":"...","session":"...","request":"...","count":2,"index":0,"word":"hello"}
WIH:{"type":"upload_word","protocol":1,"device":"...","session":"...","request":"...","count":2,"index":1,"word":"world"}
WIH:{"type":"upload_end","protocol":1,"device":"...","session":"...","request":"...","count":2}
```

网页 `WIH-new-2.0/web/serial-bridge.js` 只在 request、数量和全部有序词完整后调用整句回调；半句不会追加到网页。原有 `get_sentence` 请求仍返回 `sentence_begin/sentence_word/sentence_end`。Picker 第一次确认仍会输出 `word_confirmed`，第二次 A 不重复输出该事件。

## 6. 显示规则

- Picker 保持三行候选布局；单机确认页显示 `ENTER SOLO?` 或 `EXIT SOLO?`。
- Sentence 词框不再用“来源为本机”替代 cursor 反显。Navigate 仅当前词带下划线；InsertCursor 不显示该下划线，而显示独立竖线。
- 空句插入模式在主区域中央显示竖线。
- 删除、清句、上传分别显示独立标题和 `A:... B:CANCEL` 页脚。
- 无操作约 5 秒后只让墨水屏 hibernate，MCU、UART、`game.tick()` 和周期 `alive` 日志继续运行。连接、来词和本地输入会唤醒重绘，传输不等待屏幕恢复。

## 7. 传输协议边界

| kind | 关键字段 | 行为 |
|---|---|---|
| `WORD_OFFER` | `req`、`text`，可选 `pos` | 有 `pos` 按指定位置；无 `pos` 按接收方当前是否处于 InsertCursor 决定竖线或句首/句尾 |
| `WORD_PULL` | `req`、`pos` | 保留协议兼容，请邻居提供指定位置的 offer；新按钮流程不依赖第二次 A 发起 pull |
| `WORD_ACCEPT` | `req`、`side` | 接收方已保存入句；发送方此时才可随机 |
| `WORD_DONE` | `req` | 完成 receipt，允许以后回收 |
| `ERROR` | `req`、`code` | `BUSY` 退避；`INVALID/UNSUPPORTED` 取消相应 pending |

- 句子最多 12 词；满句或无效位置拒绝新插入并返回 `BUSY`。
- receipt 最多 8 条；未完成记录不静默淘汰，清句后仍作为防重放 tombstone。
- 热插拔重试只承诺本次上电运行期间；offer/pull/DONE pending 不保证跨断电恢复。
- 有两个直接邻居时，Picker 主动 offer 不自动选择目标。InsertCursor 本身不因第二邻居出现而退出。

## 8. 持久化

NVS magic 为 **WIH5**，保存：

- Picker 历史环、当前位置和确认状态；
- 有序句子、选择 cursor、每词来源；
- 单机标记和竖线插入位置；
- 最多 8 条 receipts。

启动接受并迁移 WIH1/WIH2/WIH3/WIH4。旧状态没有单机标记和竖线位置，迁移后使用默认值。损坏、越界、未终止或无效文本状态会被拒绝。编辑确认页不跨断电恢复；有句子或单机标记时回 Sentence/Navigate，否则回 Picker。

## 9. 软件测试

```sh
pio test -e native
node --test WIH-new-2.0/web/tests/serial-bridge.test.mjs
```

native 共 **60 项**：7 项基础协议/JSON/开放加入、8 项协议故障、38 项游戏/句子/单机/确认交互、7 项 UART 与上传测试。覆盖竖线来词定位、退出模式后的重试幂等、单机本机插入、删除/清句/上传确认、原连接与读取事件、冻结快照、分帧顺序和 UART 背压。网页 Node 测试覆盖主动上传只在完整序列后提交。

Web 双板模拟器已删除，不再作为验收路径。native 虚拟 UART 和网页模拟不能证明真实硬件时序。

## 10. 实板手动验收

旧分支仅完成过单板启动、N8R8 PSRAM、旧状态迁移、屏幕刷新和休眠前保存。本次 WIH5、新按钮、竖线显示、单机与主动上传均须重新实板验收。

1. 构建并烧录两块相同提交的 N8R8；核对 `configured=1 uart=1 identity=1`、8 MB PSRAM 和节点日志。
2. Picker 检查旋转、A 选中/再 A 发送、B 取消/shuffle，确认不再依赖双击识别。
3. Picker 长按 A：B 取消不进入；再次长按并 A 进入单机。切换 Picker/Sentence，连续插入至少三个本机词并核对竖线位置。
4. 联机接收普通词：Navigate 下 Left/Right 分别入句首/句尾；InsertCursor 下同一物理入边按竖线位置插入。
5. 检查下划线和竖线差异、编码器边界、A 返回选择、B 取消插入；确认旋转不会误删或换词。
6. 短 B 删除中间、末尾和唯一词，分别验证 A 确认和 B 取消。长 B 清句同样验证确认与取消。
7. 在 OFFER 前、入句后 ACCEPT 前、PULL/定位 OFFER 阶段断线重连；同 req 只能入句一次。特别是在竖线插入后退出模式，再验证重试不重复。
8. 先确认原单词上传和按钮读取整句不变。打开网页后长按 A 并确认，网页应只追加一次完整有序句子。
9. 上传过程中改变硬件句子，网页仍应收到确认时的冻结快照；断线或不完整序列不得追加半句。
10. 重启检查 WIH5 的句子、cursor、单机标记和竖线位置；旧 WIH4 状态应能迁移。确认页和 pending 不得宣称断电恢复。
11. 无操作超过 10 秒，面板休眠后 `alive` 和 UART 必须持续；下一次重绘先全刷。
12. 用 1、2、12 词和长 ASCII 词检查布局、省略号、竖线和页脚；按接线文档另行验证电气、真实 UART 和热插拔安全。

## 11. 已知限制

- 新交互、WIH5 迁移和主动 UART 上传尚未完成实板验收。
- 当前字体只保证 ASCII；协议可接收 UTF-8，但非 ASCII 字形和宽度显示未实现。
- 上传没有网页应用层 ACK、离线队列或断点续传；`UPLOADING` 不等于网页已保存。
- CAPTURE/READY 记录者事务和句子历史 Flash 日志仍未实现。
- 开放加入无密码学准入控制；电气热插拔安全不能由协议重连能力推定。
