# WIH 句子编辑器：交互、协议与验收

本文描述当前 `esp32-s3-devkitc-1-n8r8` 固件的 Picker、Sentence 和词传输行为。实现依据是 `src/word_game.cpp`、`src/sentence.cpp`、`src/display.cpp`、`src/storage.cpp` 及 native 测试。这里的“已支持”指软件实现；实板仅完成第 11 节记录的单板启动/显示/保存检查，**不能把本文当作完整双板硬件验收结果**。

可在电脑上按阶段操作同一交互模型，见 [双板 Web 模拟器使用说明](simulator.md)。模拟器不执行真实 WLP2 帧，也不验证真实 UART 或实体显示。

## 1. 核心模型

- 每块板始终有一个 Picker 当前候选词，但候选词不会自动成为句子的一部分。
- 句子是**最多 12 个词的有序数组**，不是“左来词 / 本机词 / 右来词”三个槽位。每项同时保存文本、来源 ID；另有一个循环 cursor 指向当前选中的词。
- 任意有效来词被插入后，接收板立即进入 Sentence 视图，成为“句子块”。发送板只有在收到匹配的应用层 `WORD_ACCEPT` 后才取消确认并随机出新候选词。
- 主动 offer 和定位 pull 都使用同一个 `WORD_OFFER` → `WORD_ACCEPT` → `WORD_DONE` 接收流程。pull 只是由句子块先发出带 `pos` 的 `WORD_PULL`，请邻居把当前候选词按该位置 offer 回来。
- 物理 L/R UART 只决定**无 `pos` 的首次主动 offer** 的默认位置：从左口收到就插到句首，从右口收到就插到句尾。它不再决定定位 pull 的句内位置，也不会在换向重连时搬动已插入的词。
- 同一个直接邻居重连、换向或丢包重试时，以 `src + boot + req` 识别同一笔接收，不重复插入。
- 词传输只在一个有效直接邻居时自动确定目标；零邻居时只提示，两个直接邻居时 A 不擅自选择目标。无连接时的本地删除/清句不受影响。

## 2. 视图、编辑模式与切换

### 2.1 状态定义

| 视图 / 模式 | 含义 | 空句是否可存在 |
|---|---|---|
| `Picker` | 三行随机选词页；无句子内容 | 是，Picker 的正常状态 |
| `Sentence / Navigate` | 显示有序句子，编码器循环选择词 | 是；删掉最后一个词后仍留在此页 |
| `Sentence / DeleteConfirm` | 对 cursor 所在词显示 `delete? y/n` | 否；空句时不会进入 |
| `Sentence / InsertSide` | 显示唯一邻居的 remote candidate，并选择在其 BEFORE/AFTER 拉取 | 是；空句固定使用位置 0 |

`InsertSide` 中的 `L:BEFORE` / `R:AFTER` 是**相对于 cursor 所在词的句内位置**，不是 UART 左右口，也不是屏幕左右邻居的物理位置。

### 2.2 切换图

```text
                         收到有效 WORD_OFFER
                ┌──────────────────────────────────┐
                │                                  ▼
┌────────┐  B清句                         ┌──────────────────┐
│ Picker │ ◄───────────────────────────── │ Sentence/Navigate │
└────────┘                                └──────────────────┘
   │ A第一次：确认当前词                     │ 编码器：循环cursor
   │ A第二次：向唯一邻居发无pos OFFER          │
   │ B：刷新，或取消确认/pending              │ A，无连接且非空
   ▼                                         ▼
Picker confirmed                     Sentence/DeleteConfirm
                                             │ A：删cursor词
                                             │ B：取消删除
                                             ▼
                                      Sentence/Navigate
                                      （可为空句）

Sentence/Navigate ──A，唯一且信息有效的直接邻居──► Sentence/InsertSide
       ▲                                               │ B：切换L BEFORE/R AFTER
       │                                               │ A：发WORD_PULL(req,pos)
       │ 收到匹配的定位OFFER并插入                       ▼
       ├──────────────────────────────────────── pull pending
       │                                               │ B：取消本机pull
       └───────────────────────────────────────────────┘
```

补充规则：

- `Navigate` 下有一个连接但 remote 信息尚未有效或邻接顺序尚未核对完成时，A 只显示 `NO LINK`，不进入 `InsertSide`。
- `Navigate` 下有两个连接时，A 显示 `CHOOSE ONE LINK`；无连接且空句时 A 不动作。
- 任意有效新来词都会把视图置为 `Sentence / Navigate`，cursor 指向新插入的词。
- 主动 offer 的 pending 期间通常仍在 Picker；收到对方来词时仍可进入 Sentence。
- offer 或 pull pending 时 A 不启动另一笔操作；B 优先取消 pull，其次取消 offer，再执行当前页面原本的 B 功能。
- B 取消 pending 只取消本机等待，不是分布式撤销：对端已经插入的词不会被远程删除。

## 3. 按键上下文表

以下“唯一邻居”还必须是已完成握手和身份校验的直接 UP 邻居；进入 `InsertSide` 另外需要有效 `NODE_INFO` 和一致的邻接顺序。

| 当前上下文 | 编码器 | A | B |
|---|---|---|---|
| Picker，未确认 | 双向无终点浏览历史/新词；改变当前候选 | 第一次按下：确认并反显当前词 | 重新随机当前词及后续候选 |
| Picker，已确认，无 pending | 先取消确认，再切换候选 | 第二次按下：唯一邻居时发无 `pos` offer；无邻居/两邻居只提示 | 取消确认，不换词 |
| 主动 offer pending | Picker 中转动会取消本机 pending、取消确认并换词；Sentence 中只移动 cursor | 忽略，避免并发新请求 | 取消本机 pending 和确认；不随机新词 |
| Sentence / Navigate，无连接 | 非空时循环 cursor；空句不动 | 非空时进入 `DeleteConfirm`；空句不动作 | 清空整个句子并回 Picker |
| Sentence / Navigate，唯一邻居 | 同上 | 进入 `InsertSide`；空句也可以 | 清句并回 Picker |
| Sentence / Navigate，两个邻居 | 同上 | 显示 `CHOOSE ONE LINK`，不发送 | 清句并回 Picker |
| Sentence / DeleteConfirm | 忽略，防止确认目标变化 | 再按 A：删除 cursor 词并回 Navigate | 取消删除并回 Navigate |
| Sentence / InsertSide，无 pull pending | 仍改变内部 cursor；发送时按最新 cursor 计算位置 | 未满 12 词时向唯一邻居发 `WORD_PULL`；满句显示 `SENTENCE FULL/BUSY` | 在 `L:BEFORE` 与 `R:AFTER` 间切换 |
| pull pending | Sentence 中只移动 cursor，不改变已经发出的 `pos` | 忽略 | 取消本机 pull，回 Navigate |

注意：InsertSide 页面显示候选词和 L/R 选项，不显示句子本身；若在此页旋转编码器，内部 cursor 会改变但页面不会展示新的目标词。因此可预测的操作顺序是先在 Navigate 选好词，再按 A 进入 InsertSide。

## 4. 插入位置、删除与容量

### 4.1 位置计算

设当前句长为 `n`，cursor 为 `i`（从 0 开始）：

| 操作 | 插入位置 | 结果 |
|---|---:|---|
| 无 `pos` offer，从接收板 Left UART 进入 | 0 | 句首 |
| 无 `pos` offer，从接收板 Right UART 进入 | n | 句尾 |
| `WORD_PULL`，空句 | 0 | 第一个词 |
| `WORD_PULL`，`L:BEFORE` | i | 所选词之前 |
| `WORD_PULL`，`R:AFTER` | i + 1 | 所选词之后 |

`pos` 必须不大于接收时的句长；否则拒绝插入。定位 pull 的 `pos` 在发请求时固定，之后旋转 cursor 不会修改这笔 pending 请求。

### 4.2 删除和清句

- Navigate 编码器按句长取模循环；从最后一个词继续旋转会回到第一个，反向也循环。
- 删除需要两次 A，中间可用 B 取消。删除后 cursor 指向原位置的后一个词；若删的是末尾词，则指向新的末尾词。
- 删除最后一个词后显示 `Empty sentence`，仍处于 Sentence，可在有唯一邻居时直接 pull 第一个词。
- Navigate 的 B 不需要二次确认，会清空全部句子并回 Picker。DeleteConfirm 的 B 只取消删除；InsertSide 的 B 只切换 BEFORE/AFTER。

### 4.3 满句和记录容量

- 句子最多 12 词；没有自动覆盖、淘汰旧词或第 13 词滚动窗口。
- 满句时，本机 A 不会发出新的 pull，并显示 `SENTENCE FULL/BUSY`。
- 满句或插入位置无效时，接收方不修改句子，对新 offer 返回 `ERROR/BUSY`。发送方保留 pending 并退避重试；可先删除或清句，再让同一请求重试成功。
- 接收去重记录最多 8 条。未完成记录不能被静默淘汰；有已完成记录时，才可为新请求回收最老的已完成记录。记录耗尽也返回 `BUSY`。

## 5. 双机逐步例子

下面用 `alpha`、`beta`、`gamma` 代表随机词，仅用于说明顺序。真实词由词库和随机数决定。

### 5.1 A 先主动传给 B，B 成为句子块

1. 将 A 的 R 口接到 B 的 L 口，两板使用同一 kit、正确 member/recorder ID，等待握手和 `NODE_INFO` 邻接核对完成。
2. A 在 Picker 显示当前词 `alpha`，B 的 Picker 当前词是多少都不会自动入句。
3. A 第一次按 A：`alpha` 反显，只表示确认，尚未发送。
4. A 第二次按 A：A 生成 `req=r1`，发送 `WORD_OFFER{req:r1,text:alpha}`，不带 `pos`，进入 `SENDING / WAIT`。
5. B 从左口收到 offer，因此把 `alpha` 插入位置 0。B 的句子为 `[alpha]`，cursor=0，并进入 `Sentence / Navigate`；这就是 B 成为句子块。
6. B 保存句子和接收记录，并返回 `WORD_ACCEPT{req:r1,side:L}`。这里的 `side:L` 是接收入边，不是 BEFORE/AFTER 选项。
7. 在 A 收到匹配 ACCEPT 之前，A 不换词，并用同一个 `r1` 重试。B 若重复收到 `r1`，只重发 ACCEPT，不再插入第二个 `alpha`。
8. A 收到 ACCEPT 后取消 pending/确认，随机出 `beta`，并发送 `WORD_DONE{req:r1}`。B 用 DONE 把接收记录标为已完成；句子 `[alpha]` 保持不变。

### 5.2 B 再把 A 的新词拉到所选词右侧

1. B 在 Navigate 中选中 `alpha`。A 的当前候选已是 `beta`，B 通过 `NODE_INFO/STATE_EVT` 看到这个 remote candidate。
2. B 按 A 进入 `InsertSide`。页面显示 `CANDIDATE beta`，默认选中 `L:BEFORE`。
3. B 按 B 切到 `R:AFTER`。因为 `alpha` 的 cursor=0，目标位置为 `0+1=1`。
4. B 再按 A，生成 `req=r2`，发送 `WORD_PULL{req:r2,pos:1}`，保留 pull pending。
5. A 收到直接邻居 B 的 pull，且自己没有冲突的 offer pending，就用当前词 `beta` 回应 `WORD_OFFER{req:r2,text:beta,pos:1}`。A 此时进入 offer pending，**仍不能提前随机**。
6. B 按 `pos=1` 插入，句子变为 `[alpha,beta]`，cursor=1，退出 pull pending 并回 Navigate。物理 L/R 不重新解释这个位置。
7. B 返回 ACCEPT；A 收到后才随机出 `gamma`，再发 DONE。B 保留 `[alpha,beta]`。
8. 若第 3 步选择 `L:BEFORE`，则请求位置为 0，最终应是 `[beta,alpha]`。空句时 BEFORE/AFTER 都得到位置 0。

### 5.3 中途断线

- 若第 4 步后断线，B 保留 `r2` 和 `pos=1`，A 保留当前 `beta`；重连后 B 用同一 `r2` 重发 pull。
- 若第 5～6 步后断线，B 已保留 `[alpha,beta]` 和接收记录，A 保留 offer pending；重连后的重复 OFFER/ACCEPT 不会再次插入 `beta`。
- 上述“保留”指本次上电运行期间的热插拔恢复，不承诺断电后保留 pending，见第 9 节。

## 6. 屏幕布局与状态文本

### 6.1 Picker

- 画布为 200×200。上一词、当前词、下一词组成三行，当前词位于 `y=69, h=62` 的中央选词带。
- 未确认时显示边框和左右箭头；确认后中央黑底白字。
- 当前词依次尝试 Bold 18pt、Bold 12pt、Regular 12pt。确认/取消确认使用中央 200×62 局部窗口，其他多行变化使用整屏范围的快速局刷。

### 6.2 Sentence / Navigate 与 DeleteConfirm

- 顶部为状态文本，无状态时显示 `SENTENCE`；句子页不显示旧的在线参与数或三槽方向计数。
- 主框为 `(4,28,192,136)`，文本区域从 x=10 起、宽 180。词按数组顺序从左到右、从上到下换行。
- cursor 所在词黑底白字；其余词白底黑字。长词按实际字体宽度加 `...` 缩略。
- 排版依次尝试 FreeSans 12pt（行高 29）、FreeSans 9pt（行高 23）和内置小字体（行高 10），最多绘制 12 项。
- 空句显示 `Empty sentence`。
- Navigate 页脚：`ROTATE:SELECT A:DEL/PULL`、`B:CANCEL`。
- DeleteConfirm 页脚：`delete? y/n`、`A:YES  B:NO`。

### 6.3 Sentence / InsertSide

- 顶部仍显示状态；主框内显示 `CANDIDATE` 和 remote candidate 文本。
- 两个选项为 `L:BEFORE` 与 `R:AFTER`，当前选项反显；页脚为 `A:INSERT B:L/R`。
- 候选来自唯一邻居的当前 Picker 词，不是对方句子中选中的词。

### 6.4 状态文本

| 屏幕文本 | 含义 |
|---|---|
| `NO LINK` | 没有可用目标，或 remote/邻接信息尚未有效 |
| `CHOOSE ONE LINK` | 同时有两个直接邻居，当前交互不自动选择 |
| `SENDING / WAIT` | offer 或 pull 正在等待应用层完成 |
| `RECEIVED FROM L/R` | 新词从哪个物理口到达；不代表句内插入侧 |
| `SENT - NEW WORD` | 已收到 ACCEPT，发送方已随机新词 |
| `SENTENCE FULL/BUSY` | 满句、位置/记录暂不可接收，或收到 BUSY |
| `TRANSFER REJECTED` | 对端返回 INVALID/UNSUPPORTED，本机相应 pending 已取消 |

## 7. 词传输协议

### 7.1 消息与字段

所有词传输只接受与当前入边直接邻居的 `src/boot` 相符的消息。WLP2 的 LACK 只说明链路帧被接收，**不等于词已入句**。

| kind | body 关键字段 | 作用 |
|---|---|---|
| `WORD_PULL` | `req`、 `pos` | 句子块请唯一邻居用当前候选词发定位 offer |
| `WORD_OFFER` | `req`、`text`，可选 `pos` | 提供词；有 `pos` 时严格按句内位置插入 |
| `WORD_ACCEPT` | `req`、`side` | 接收方确认应用层已记录；`side` 为接收入边 L/R |
| `WORD_DONE` | `req` | 发送方已处理 ACCEPT；接收方据此完成并允许以后回收记录 |
| `ERROR` | `req`、`code` | `BUSY` 退避重试；`INVALID/UNSUPPORTED` 取消对应本机 pending |

`req` 是由 32 个十六进制字符组成的请求 ID；文本协议上限为 96 字节，空文本和控制文本无效。JSON 接收路径还校验 UTF-8。应用层 offer/pull 在成功排入发送后按约 700ms 调度重试，发送未能排队时按约 250ms 重试；收到 `BUSY` 后约 1000ms 再试。底层 WLP2 另有 LACK/LBUSY、CRC32、序号去重和链路重传。

### 7.2 主动 offer 时序

```text
发送方 Picker                         接收方
    │ A确认，再A发起                      │
    │ WORD_OFFER {r,text}  ─────────────► │ 校验直接邻居、容量和req
    │                                    │ 无pos：L入边→0，R入边→句尾
    │                                    │ 插句、存receipt、进入Sentence
    │ ◄───────────── WORD_ACCEPT {r,side} │
    │ 取消pending，随机新词                │
    │ WORD_DONE {r} ────────────────────► │ receipt标记completed
```

接收方先插句再 ACCEPT；发送方不能在 LACK 或发出 OFFER 后提前随机。DONE 不参与“是否入句”的判定。

### 7.3 定位 pull 时序

```text
句子块 / 拉取方                       候选词提供方
    │ 选cursor和BEFORE/AFTER             │
    │ WORD_PULL {r,pos} ───────────────► │ 固定当前候选词
    │                                    │ 建立同req的定位offer pending
    │ ◄──────── WORD_OFFER {r,text,pos}  │
    │ 校验pos并插句，清pull pending       │
    │ 保存receipt，回Navigate            │
    │ WORD_ACCEPT {r,side} ────────────► │ 仅现在随机新候选词
    │ ◄──────────────── WORD_DONE {r}    │ （方向如图，由提供方发给拉取方）
    │ receipt标记completed               │
```

上图中最后一条 DONE 实际由右侧提供方发往左侧拉取方；它不影响已经确定的句内位置。若提供方已有其他 offer pending，则返回 `BUSY`；同 req 且位置一致的重复 pull 会促使原 offer 重发，位置不一致则返回 `INVALID`。

### 7.4 幂等规则

- 接收键为 `src + boot + req`。重试时文本、是否带 `pos` 及位置必须一致，否则返回 `INVALID`。
- 同一请求第一次成功时才插入和增加接收计数；之后只更新必要的入边信息并重发 ACCEPT。
- 同 req 换向重连不会重算或移动句内位置。只有一笔**全新的无 pos offer** 才按当时的入边选择句首/句尾。
- 接收记录跨清句保留，形成防重放 tombstone：清句后旧 offer 重到，不会把旧词重新插回来。
- WLP2 的 `src + boot + seq` 去重是链路/网络层保护，不能替代应用层 `req` 接收记录。
- 对不匹配当前 pending 对端或 req 的 ERROR/ACCEPT 不予采纳。

## 8. 热插拔与取消阶段表

| 断线/故障发生阶段 | 发送/提供方 | 接收/拉取方 | 重连后的预期 |
|---|---|---|---|
| 主动 OFFER 尚未被接收 | 保留 req、文本和 pending，不随机 | 句子不变 | 同 req 重发，至多插入一次 |
| 已插句，ACCEPT 丢失 | 仍 pending，保持原词 | 保留句子与 receipt | 重复 OFFER/ACCEPT 收敛，不重复插入 |
| ACCEPT 已收到，DONE 丢失 | 已随机新词 | 句子保留，receipt 尚未完成 | 不重复插句；未完成 receipt 不被静默淘汰 |
| PULL 尚未变成 OFFER | 当前候选词不变 | 保留 pull req 和 pos | 同 req 重发 PULL |
| 定位 OFFER 已插入，ACCEPT 丢失 | 保留 offer pending，不随机 | 已清 pull pending，句子和 receipt 保留 | 同 req 重试 ACCEPT/OFFER，不重复插入 |
| 换向重连 | pending 仍按邻居 ID 找直接链路 | 原词位置不变 | receipt 入边可更新，句内位置不迁移 |
| 本机按 B 取消 offer/pull | 清除对应本机 pending | 不发送远程撤销 | 对端已插入的内容不回滚；迟到且无关的 ACCEPT 被忽略 |

热插拔期间已建立的句子不因断线清空。电气上的安全带电插拔并未因此得到证明；UART 触点保护、倒灌和 ESD 仍须按 `wiring.md` 单独实测。

## 9. 持久化与断电边界

### 9.1 WIH4 保存内容

NVS magic 为 `WIH4`，保存：

- Picker 历史环、首尾/当前位置和确认状态；
- 句子的**有序词数组、cursor、每词来源 ID**；
- 最多 8 条 receipts，包括 `src/boot/req/text`、入边、是否定位、位置、完成标志和回收顺序。

删除、清句、收词、DONE、确认/取消确认和休眠前等变化会请求保存。普通 Picker/Sentence 滚动不逐次写 NVS，以避免 Flash 磨损；休眠保存会包含最新 cursor，连续滚动期间突然断电则可能只恢复到最后写入点的光标位置。NVS 写入失败会记录日志并按约 1 秒退避重试，不能视为保存成功。

启动时接受并迁移 WIH1、WIH2、WIH3：

- WIH1 迁移 Picker，不含句子。
- WIH2 把旧左右词按原顺序压缩迁入有序数组，来源为空。
- WIH3 额外迁移来源和 receipts；旧 receipts 没有定位字段，按无定位记录处理。
- 迁移后的 cursor 使用有效默认值；后续保存写成 WIH4。损坏、越界、未终止或无效文本状态会被拒绝，而不是越界使用。

### 9.2 不保证跨断电恢复的内容

- **offer pending、pull pending、DONE 的内存发送状态不保存。** 断电前正在等待的 req 不保证重启后自动继续。
- **Navigate/DeleteConfirm/InsertSide、BEFORE/AFTER 选项不作为编辑会话恢复。** 有句子时重启回 Sentence/Navigate，无句子时回 Picker，BEFORE/AFTER 回默认值。
- 句子、cursor、来源和 receipts 的保存不代表一次未完成的分布式传输能原子恢复。若一端已保存入句而另一端在 ACCEPT 前断电，重启后可能保留句子但不再保留原 pending；需要用户重新操作，旧 req 的防重放记录仍防止重复插入。
- 普通复位和重新上电只从 NVS 恢复；当前固件不包含 MCU 深睡、RTC 唤醒或 RTC 状态镜像路径。

无操作约 5 秒后只让墨水屏 hibernate，MCU、UART、握手、`game.tick()` 和重试继续运行。屏幕已休眠且无绘制变化时不会反复保存；连接、来词或本地输入会刷新空闲计时并立即全刷恢复显示，不需要按 A 唤醒。即时 OFFER/PULL 不等待屏幕恢复。

因此，“断线保留 req 重试”仅描述运行中的热插拔；不能写成“断电后续传”。

## 10. 软件测试：46 项

运行：

```sh
pio test -e native
```

native 使用同一套协议、Picker、Sentence 和 WordGame 源码，以固定随机种子、虚拟时间、脚本按键和可控虚拟 UART 测试。套件共 46 项：5 项基础协议/JSON、8 项协议故障、33 项游戏/句子编辑。双板 Web 模拟器另有 14 项 Python 模型与 HTTP 测试，命令见 [模拟器使用说明](simulator.md#8-测试)。

### 10.1 基础协议与 JSON（5）

| # | 测试 | 覆盖点 |
|---:|---|---|
| 1 | `test_crc_and_json` | CRC、JSON 重复键/数字/Unicode/控制文本/长度、帧编码 |
| 2 | `test_json_depth_roundtrip` | JSON 深度边界和序列化 |
| 3 | `test_handshake_information_and_disconnect` | 握手、sid/topology、NODE_GET、断线重连 |
| 4 | `test_foreign_kit_is_rejected` | 不同 kit 拒绝连接 |
| 5 | `test_recorder_role_mismatch_is_rejected` | recorder/member 角色校验 |

### 10.2 协议故障（8）

| # | 测试 | 覆盖点 |
|---:|---|---|
| 6 | `test_repeated_hello_and_frame_recovery` | 重复 HELLO、CRC 损坏、LACK 丢失、重复帧 |
| 7 | `test_failed_send_preserves_next_tx` | 发送失败后序号状态可继续 |
| 8 | `test_one_way_failure_and_stale_session` | 单向故障、超时、旧 sid 帧拒绝 |
| 9 | `test_three_node_forward_and_reorder` | 三块短链转发和重排 |
| 10 | `test_buffered_pong_prevents_timeout_reset` | 延迟 PONG 与链路活动 |
| 11 | `test_broadcast_shares_seq_across_paths` | 多路径广播去重 |
| 12 | `test_busy_and_interbyte_timeout` | LBUSY、字节间超时 |
| 13 | `test_crc_prefix_resync` | CRC 前缀噪声后的重同步 |

### 10.3 Picker、传输和句子编辑（33）

| # | 测试 | 覆盖点 |
|---:|---|---|
| 14 | `test_picker_and_buttons` | 无限历史/新词、相邻不重复、刷新 |
| 15 | `test_left_transfer_and_node_info` | 双确认主动传词、接收方入句、ACCEPT 后随机 |
| 16 | `test_reverse_transfer_goes_to_right` | 反向无 pos offer 的默认句尾/入边行为 |
| 17 | `test_lost_accept_is_idempotent` | ACCEPT 丢失不重复插入 |
| 18 | `test_hotplug_during_transfer` | OFFER 阶段热插拔、同 req 续试 |
| 19 | `test_reconnect_after_lost_accept` | 入句后断线、重连幂等 |
| 20 | `test_repeated_full_game_cycles` | 12 轮主动/拉取循环和反复重连 |
| 21 | `test_cancel_restore_and_no_link` | 无连接提示、取消、刷新、清句 |
| 22 | `test_three_contributions_and_two_link_guard` | 双邻居守卫、多块来词和离线保句 |
| 23 | `test_refresh_never_returns_current_word` | 刷新不返回当前词 |
| 24 | `test_simultaneous_bidirectional_transfer_keeps_sentence` | 双向同时主动传词 |
| 25 | `test_error_requires_pending_peer` | ERROR 的对端和 pending 匹配 |
| 26 | `test_cancel_requests_save` | 取消/滚动触发保存 |
| 27 | `test_reversed_reconnect_keeps_word_position` | 换向重连不移动已收词 |
| 28 | `test_restored_receipt_blocks_replay_after_clear` | 恢复 receipt、清句后的旧 req 防重放 |
| 29 | `test_unknown_restored_source_appends_without_overwrite` | 旧来源为空时安全插入而不覆盖 |
| 30 | `test_positioned_pull_order_and_cursor_from_each_side` | 两侧定位 pull、顺序和 cursor |
| 31 | `test_sentence_cursor_delete_confirm_cancel_and_clear` | 循环选择、删除确认/取消、清句 |
| 32 | `test_empty_sentence_can_pull` | 空 Sentence 拉取首词 |
| 33 | `test_hotplug_preserves_pull_request_without_duplicate` | PULL 热插拔、同 req、无重复 |
| 34 | `test_positioned_pull_lost_accept_is_idempotent` | 定位 pull 的 ACCEPT 丢失幂等 |
| 35 | `test_sentence_capacity_and_position_bounds` | 12 词上限、位置边界、满句拒绝 |
| 36 | `test_restore_sentence_cursor_order_and_replay_tombstone` | WIH4 所需顺序/cursor/sources/receipt 恢复逻辑 |
| 37 | `test_simultaneous_bidirectional_positioned_pulls` | 双向同时定位 pull |
| 38 | `test_insert_mode_disconnect_and_cancel_return_to_navigation` | 插入页断线/取消后返回导航，可继续删除 |
| 39 | `test_remote_candidate_change_requests_display` | 对端换词更新候选并请求重绘 |
| 40 | `test_incomplete_receipts_are_not_evicted` | 未完成 receipt 不淘汰、DONE 后回收 |
| 41 | `test_delete_last_word_keeps_empty_sentence_navigation` | 删除最后一词后停留空 Sentence/Navigate，cursor 归零且 A 不误进入其他模式 |
| 42 | `test_cancelled_offer_does_not_retry` | B 取消 offer 后不再重试，候选词保持不变 |
| 43 | `test_cancelled_pull_does_not_retry` | B 取消 pull 后回 Navigate、不再重试，对端不建立 offer |
| 44 | `test_pull_position_is_fixed_when_cursor_moves` | pull 发出后移动 cursor 不改固定 pos，新词仍插到原请求位置 |
| 45 | `test_delete_confirm_is_stable_when_link_connects` | DeleteConfirm 期间建立连接不改变删除目标或模式 |
| 46 | `test_second_link_exits_insert_side` | 第二邻居出现时退出 InsertSide，禁止自动选目标或发起 pull |

这些测试不验证字体在实际面板上的效果、电气安全、真实 UART 时序、PSRAM、休眠电流或残影。

## 11. 实板手动验收步骤

本分支已在本机一块 N8R8 上完成有限的启动检查：固件烧录哈希校验通过；启动日志确认 8 MB PSRAM；旧 WIH3 状态成功迁移并恢复 Picker；墨水屏完成全刷和快速局刷；空闲进入休眠前完成 NVS 保存，未再出现重启。首次实板启动曾在 NVS 迁移时因主任务栈上的多个大状态副本触发 `Double exception`，现已将启动与保存缓冲区改为静态存储并复测通过。

以下完整步骤仍**尚待实板执行**，尤其是双板握手、收词后的 Sentence 页面、删除、定位 pull 和故障恢复。本机写入真实节点 ID 后已确认 `configured=1、uart=1、identity=1`；当前 `links=0`，另一块板仍需配置真实 ID，不能记为双板通过。接线、电气风险和 RGB 差异见 `wiring.md`；显示和休眠的完整验收见 `display-design-requirements.md`。

### 11.1 准备和基本交互

1. 为每块板在 `src/config.h` 配置真实 kit、recorder、member ID，确认角色与成员表一致；不要沿用占位 ID 期待通过校验。
2. 按固定 N8R8 配置执行 `pio run` 和 `pio run -t upload`。串口确认无持续 Busy Timeout，并记录 `psram: found=1` 及 8 MB 容量；若日志不符，先停止功能验收。
3. 单机检查 Picker：双向旋转、相邻词不重复、A 第一次反显、B 取消、未确认时 B 刷新；确认/取消只刷中央区域。
4. A-B 直连并完成握手。A 两次按 A 后，确认 A 在 ACCEPT 前不换词，B 收到后进入 Sentence；完成后 A 才随机。B 的 Picker 当前词不得自动插入句子。
5. 交换主动方向，再交换物理接线做全新请求：无 pos offer 应分别按接收入边落在句首/句尾；不要把这个规则用于定位 pull。

### 11.2 Navigate、删除和清句

6. 收至少 3 词，旋转编码器验证首尾循环和选中词反显。无连接时按 A 应出现 `delete? y/n`；B 取消后句子不变，再 A→A 删除目标词。
7. 删除中间、末尾和唯一词，检查 cursor 规则。删到空句应显示 `Empty sentence` 且仍在 Sentence。
8. 在 Navigate 按 B，确认整句清空并回 Picker；分别确认 DeleteConfirm 的 B 不清句、InsertSide 的 B 只切换 L/R。

### 11.3 定位 pull

9. 在非空句中选择中间词，A 进入 InsertSide，核对 remote candidate 是对端当前 Picker 词。选 L BEFORE 拉取，确认新词在所选词之前；选 R AFTER 再拉新词，确认在所选词之后。
10. 保持同一物理接线，仅改变 BEFORE/AFTER，证明 UART 左右不改变定位 pull 的句内位置。换向重连后旧句顺序仍不变。
11. 从空 Sentence 直接 A→A 发起 pull，确认能得到位置 0 的第一个词。
12. 在 InsertSide 满 12 词时按 A，确认不发起 pull 并显示 `SENTENCE FULL/BUSY`；再尝试第 13 个远端 offer，确认不入句。删除一词后重试应可成功。
13. 有两个有效直接邻居时按 A，确认显示 `CHOOSE ONE LINK` 且不发送；remote 信息未就绪时不得把陈旧候选当成有效目标。

### 11.4 故障、持久化和显示

14. 分别在 OFFER 前、入句后 ACCEPT 前、PULL 后、定位 OFFER 后制造断线/接触抖动。重连后每笔 req 只能入句一次，发送方仅在 ACCEPT 后随机，句子不得因拔线消失。
15. 在上述各阶段按 B 取消本机 pending，确认本机不再重试；同时记录对端已入句内容不会远程撤销这一边界。
16. 普通复位和完全断电重上电分别检查 NVS 中的句子顺序、cursor 和已收内容；重启后应回 Navigate 而不是 DeleteConfirm/InsertSide，pending 不应被宣称会自动续传。另保持无操作超过 10 秒，确认面板休眠后 `alive` 和 UART 仍持续。
17. 在 ACCEPT 前断电形成“一端入句、另一端 pending 丢失”的边界情况，确认旧 receipt 不会导致重复插入；需要继续时由用户发起新操作。
18. 用 1、2、12 词和长 ASCII 词检查换行、cursor 反显、省略号、InsertSide 候选页和页脚；连续局刷后检查残影并按设计执行全刷。
19. 实测真实 UART 延迟、单侧掉电倒灌、电源斜坡、单线断开、ESD、联机功耗和持续监听功耗。native 的虚拟时间不能替代这些结果。
20. 确认没有把 CAPTURE/READY 当作已实现的冻结保存流程；相关记录者事务和电脑底座导出仍需另行开发验收。

## 12. 已知限制

- **完整硬件验收未完成**：仅通过第 11 节所述单板启动、Picker 显示和保存检查；Sentence 实板交互、电气和真实 UART 尚未验收。46 项 native 测试和 14 项模拟器测试通过不等于硬件通过。
- **ASCII 字体**：当前 FreeSans/内置字体路径只保证内置英文词库的 ASCII 字形。协议能接收其他 UTF-8 文本，但非 ASCII 字形和宽度显示未实现。
- **真实 ID 仍需配置**：`src/config.h` 中的 kit、recorder、member 占位/示例值必须替换成实际套件配置，未知成员和角色不匹配会被拒绝。
- **CAPTURE/READY 未实现**：记录者冻结保存事务、句子历史 Flash 日志和电脑底座导出不在当前实现内。
- **pending 与编辑会话不跨断电保证**：见第 9 节；热插拔重试不能解释为断电续传。
- **取消不是分布式事务**：B 只取消本机 pending，不会删除对端已经接收的词。
- **双邻居不选目标**：有两个直接邻居时必须调整拓扑，当前 UI 不提供邻居选择列表。
- **词传输要求直接邻居**：虽然 WLP2 支持短链转发，WORD_PULL/OFFER 的应用层直接邻居校验不允许隔块传词。
- **InsertSide 不展示 cursor 目标**：该页旋转会改内部 cursor，但屏幕只显示候选与 L/R；应在 Navigate 先选好目标词。
- **安全热插拔未证明**：协议可重连不表示未加保护的 GPIO UART 可安全带电插拔。
