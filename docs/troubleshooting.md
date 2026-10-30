# WIH 通信、休眠与双板排查手册

本文给现场开发和排查人员使用。目标不是记住协议细节，而是能根据两块板的日志快速判断：身份是否有效、链路是否建立、数据是否到达、屏幕是否只是面板休眠，以及下一步应该检查什么。

## 1. 先理解当前设计

### 1.1 不再使用白名单

两块陌生模块接触后，只要满足以下条件就允许建立连接：

- 使用相同的 `WIH_KIT_ID`，表示运行同一个应用。
- 都使用 WLP2 v3 固件，JSON、CRC 和字段格式正确。
- 一个使用 L 口，另一个使用 R 口，物理 TX/RX 交叉且共地。
- 双方节点 ID 不相同；若相同但设备指纹不同，固件会自动换号并重握。

不需要配置 `WIH_MEMBER_x_ID`，也不需要 recorder 白名单。节点只学习实际连接的邻居，不需要提前知道全网名单。详细协议见 [开放加入与冲突恢复](open-join.md)。

### 1.2 四种身份信息各自解决什么问题

| 字段 | 人会怎么理解 | 重启后是否变化 | 排查用途 |
|---|---|---|---|
| `id` | 模块当前使用的地址 | 通常不变；冲突换号会改变 | OFFER/PULL 的目的地址和接收来源 |
| `boot` | 这一次启动的会话编号 | 每次启动或换号改变 | 防止旧会话数据被当成当前数据 |
| `fp` | 物理设备指纹 | 不变 | 判断“同 ID”是同一设备还是两台不同设备 |
| `kit` | 应用标识 | 不变 | 不同应用不建立连接 |

指纹不是密码，开放加入也不提供身份认证。任何使用相同固件和 kit 的陌生模块都可以在物理接触后加入。

### 1.3 只有面板休眠，MCU 不休眠

无操作约 5 秒后，墨水屏执行 hibernate 以保护面板。MCU、UART、握手、`game.tick()` 和重试必须继续运行。当前源码不包含 MCU deep sleep、RTC 唤醒或 RTC 状态镜像。

面板恢复显示和通信恢复是两件事：

- 新连接或新数据产生显示变化时，面板重新初始化并先全刷。
- OFFER/PULL 不等待面板恢复，MCU 可以立即处理。
- 不需要按 A 唤醒 MCU。
- 面板已休眠且没有绘制变化时，不能反复写 NVS。

## 2. 正常日志基线

第一板或第二板正常启动时，应看到类似信息：

```text
wlp2: configured=1 uart=1 identity=1 id=0000ac3a2a6b4580 links=0
boot: reset=1
psram: found=1 size=8386231 free=8385495
restored from NVS, word=searches
setup done
alive ms=3216 ... links=0 pending=0 uartErr=1 ...
```

约 5 秒无操作后：

```text
_PowerOff : ...
display: hibernate; MCU/UART awake
alive ms=9216 ... links=0 pending=0 uartErr=1 ...
alive ms=11216 ... links=0 pending=0 uartErr=1 ...
```

这里的 `_PowerOff` 是面板驱动输出，不代表 MCU 已休眠。后面持续出现的 `alive` 才说明 MCU 仍在工作。

Pogo 握手成功后，双方应出现：

```text
alive ... links=1 ...
```

两块板的 `links` 都应变为 1。只有一侧为 1 时，不能按完整双向链路验收。

## 3. 按顺序排查，不要跳步

### 第一步：确认两块板运行同一提交

在两边分别执行：

```sh
git log -1 --oneline
git status --short --branch
```

本地未提交的修改不会出现在另一块板上。WLP2 v2 白名单固件与 v3 开放加入固件不兼容，不能混合测试。

### 第二步：检查启动身份

正常值：

```text
configured=1 uart=1 identity=1
```

如果为 0：

- `uart=0`：UART1/UART2 缓冲或初始化失败，先查固件日志和硬件，不要检查 member。
- `identity=0`：本机节点 ID、boot、kit 或指纹格式无效，或者随机函数不可用。当前代码不再因为“陌生 member”返回 0。
- `configured=0`：以上任一条件失败，先修复本机，Pogo 对端无法弥补。

首次没有 `wih/node_id` 时会回退到 MAC 派生 ID。当前实现使用 `Preferences::isKey()` 检查，正常的首次启动不应打印 `NOT_FOUND` 错误。

### 第三步：检查 Pogo 的每一条线

固定 A.R ↔ B.L 示例：

| 方向 | 发送端 | 接收端 |
|---|---|---|
| 第一板 → 第二板 | 第一板 GPIO15 / R口 TX | 第二板 GPIO18 / L口 RX |
| 第二板 → 第一板 | 第二板 GPIO17 / L口 TX | 第一板 GPIO16 / R口 RX |
| 公共参考地 | 第一板 GND | 第二板 GND |

只确认“IO15 接 IO18”只验证了一个方向。还必须检查 IO17→IO16 和 GND。不要连接两板电源正极，不要 TX 接 TX 或 RX 接 RX。

电气热插拔、倒灌和 ESD 风险不能用协议测试排除，详见 [接线说明](wiring.md)。

### 第四步：等待双方 links=1

- 双方 `identity=1` 但 `links=0`：优先检查两板提交版本、kit、Pogo 方向和是否有真实 ID 冲突，不要再添加 member ID。
- 单边 `links=1`：通常是另一方向 TX/RX、对端会话变化或链路尚未收敛，继续看双方日志。
- `links` 反复 0/1：检查接触、共地、供电、3 秒链路超时和旧固件休眠行为。
- 同 ID 同指纹：不会建立连接，因为无法区分自身和对面；这通常表示自身回声、复制身份或测试配置错误。
- 同 ID 不同指纹：应自动换号，而不是拒绝陌生模块。

## 4. `uartErr` 应该怎么解读

`alive ... uartErr=N` 是 UART 驱动累计错误计数，不是当前连接数，也不是丢包总数。

- **数值保持不变**：表示历史上发生过错误，但不能仅凭非零判断当前接线仍有问题。重启后先记录初始值，再观察是否增长。
- **持续增长**：更可能有帧错误、噪声、接触抖动、波特率/格式不一致或电气问题。当前应为 115200、8N1、3.3V UART。
- **uartErr 增长且 links=0**：先断电复核 TX/RX 和 GND，再检查两板是否运行相同协议版本。
- **uartErr 不增长但 links=0**：UART 没收到可接受的新帧，继续查对端是否发送、协议版本、kit、ID 冲突和物理方向。

不要用 `uartErr=0` 代替 `links=1` 验收，也不要仅因 `uartErr` 非零就重写协议。

## 5. ID 冲突与 NVS 排查

### 5.1 正常冲突恢复日志

同 ID、不同指纹时，双方应分别生成新 ID，并出现：

```text
wlp2: reassigned id=<新节点ID> saved=1
```

随后重新 HELLO，最终 `links=1`。

`saved=1` 表示新 ID 已写入 NVS `wih/node_id`。重启后应继续使用该 ID，不再回退到 MAC 派生值。

### 5.2 `saved=0`

当前运行仍可使用新 ID，但重启后可能恢复旧 ID并再次冲突。检查：

- NVS 是否可写，是否有其他初始化/写入错误。
- 是否反复重启而没有给 NVS 写入时间。
- 日志中的新 ID是否与实际 `wlp2: ... id=...` 一致。

不要通过恢复 member 白名单掩盖保存失败。

### 5.3 一直换号、无法稳定

可能原因：

- 确实存在多个不同指纹设备使用同一 ID。
- 两边仍运行不同版本，HELLO 无法正确解析。
- 随机新 ID再次碰撞；协议应继续执行“检测→换号→重握”闭环。
- Pogo 接触导致 HELLO 会话不断重来。

记录至少 10 秒双方 `reassigned`、`wlp2:` 和 `alive`，判断是冲突没有解决，还是链路已经解决但物理连接不稳定。

## 6. 已连接但不能传词

按以下顺序检查：

1. 双方必须 `links=1`，并且 NODE_INFO/邻接信息已经有效。
2. 在 Picker 第一次按 A 只是确认，第二次才立即 OFFER。
3. `pending` 是已发起后等待应用层 ACCEPT，不是发送前等待，也不表示必须手动推进。
4. 接收方句子最多 12 词；满句或接收记录暂时不可用时返回 BUSY，发送方保留同一 req 重试。
5. WORD_OFFER/PULL 只允许直接邻居，三块短链拓扑不能隔块传词。
6. 接收方自己的 Picker 当前词不会自动入句；只有有效来词才进入 Sentence。
7. 已连接时 Navigate 的 A 进入 InsertSide，不是删除。要删除可先断开连接，再 A→A；DeleteConfirm 的 B 只取消。

定位 pull 的 BEFORE/AFTER 只由 cursor 和 `pos` 决定，不由物理 L/R 决定。发送方只有收到 ACCEPT 后才换候选词。

## 7. 面板、NV Save 与“看起来像死机”

### 面板 `_PowerOff` 后没有新画面，但 alive 持续

这是正常的面板 hibernate。只有新内容到来才重新初始化并全刷；没有内容变化时画面保持，不需要重绘。

### 面板 `_PowerOff` 后 alive 也停止

这不符合当前固件。先确认实际烧录提交，检查是否仍是旧 MCU deep sleep 固件。当前源码搜索结果不应存在 `esp_deep_sleep_start()`、`esp_sleep_*`、RTC 唤醒或 RTC mirror。

### 每个循环都写 NVS

不符合设计。`hibernateDisplayWhenIdle()` 在面板已休眠且 `dirty=false` 时必须直接返回。检查该守卫是否被改动，或网络显示事件是否持续产生新的 dirty，而不是简单删除 NVS 保存。

### 重启后 ID 或句子状态不符合预期

- 节点 ID 与 WIH4 句子状态分开保存：`wih/node_id` 对 `wih/state`。
- 句子、cursor、来源和 receipts 可从 NVS 恢复。
- offer/pull/DONE pending 不保证断电续传；运行中断线重试与断电恢复是不同边界。

## 8. 一次完整验收要收集什么

请同时保留两块板至少 15 秒日志，并附以下信息：

1. 两边的 `git log -1 --oneline`。
2. `src/config.h` 中的 `WIH_KIT_ID`，不要贴任何私密凭据。
3. 启动时的 `wlp2: configured=... uart=... identity=... id=... links=...`。
4. 启动后的多条 `alive ... links=... pending=... uartErr=...`。
5. 面板休眠前后的 `display: hibernate; MCU/UART awake` 和后续 `alive`。
6. 若发生冲突，双方 `reassigned ... saved=...`。
7. 实际 Pogo 接线，分别说明两个 TX/RX 方向和 GND，而不是只写“已交叉”。
8. 传词时的按钮顺序、pending 状态、句子内容和 cursor。

这些信息足以区分版本、身份、单向接线、冲突恢复、面板休眠和应用层 BUSY，避免靠猜测修改代码。

## 9. 软件验证与实板验证的分工

```sh
pio test -e native
python3 -m unittest simulator.test_simulator -v
```

当前 49 项 native 测试覆盖开放加入、同设备重启、ID 冲突换号及恢复后 OFFER/PULL；14 项 Web 测试覆盖模拟器模型与 HTTP 操作。它们不能证明真实 Pogo、电气安全、NVS 实板写入或面板行为。

按 [双板 SOP](hardware-sop.md) 继续执行 N8R8 构建、烧录、超过 10 秒持续监听、双方 links=1、即时 OFFER 和 Sentence 验收。只有这些证据齐全后，才应更新 PR 或 merge。
