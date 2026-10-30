# WIH 双板 Web 模拟器

本模拟器用于在电脑上操作 A/B 两块虚拟板，检查 Picker、Sentence、主动 offer、定位 pull、删除、断线和 pending 等交互。实现位于 `simulator/`，由 Python 标准库 HTTP 服务、确定性交互模型和原生 HTML/CSS/JS 页面组成。

它是交互模型，不是固件仿真器；协议和实板边界见[与真实固件的边界](#7-与真实固件的边界)。

## 1. 安装要求

- 安装 **Python 3**，运行和测试都只使用标准库，不需要 `pip install`、虚拟环境或第三方 Python 包。
- 使用能运行现代 JavaScript 的浏览器打开本地页面；页面不依赖 CDN，也不需要 npm、Node 包或前端构建。
- Node.js 是**可选**工具，只用于执行 `node --check` 检查 JS 语法，不是启动模拟器的依赖。

以下命令均从仓库根目录执行。

## 2. 启动与停止

```sh
python3 simulator/server.py
```

默认只监听本机 `127.0.0.1:8000`。终端显示地址后，在浏览器打开：

```text
http://127.0.0.1:8000/
```

端口被占用时可换端口，例如：

```sh
python3 simulator/server.py --port 8080
```

`--port 0` 可让系统自动分配端口，实际地址以终端输出为准。按 `Ctrl+C` 停止服务。状态只保存在当前 Python 进程内，重启服务或使用“重置”都会回到初始状态；模拟器不提供断电持久化。

## 3. 界面与操作规则

### 3.1 全局控制

- **连接 / 断开 A.R ↔ B.L**：切换唯一拓扑。A 固定使用 R 口，B 固定使用 L 口。重新连接时，模拟器会自动重试仍保留的 pending。
- **重试等待中的传输**：仅在断线、满句或对端冲突导致 pending 未完成后使用；正常空闲传输不需要点击。
- **重置**：经确认后清空两块板的句子、候选词、pending 和旧日志，恢复初始断线状态。

底部“操作与传输日志”记录按钮、PULL/OFFER/ACCEPT/DONE、插入位置和取消等事件；最新事件在前，最多保留 200 条。

### 3.2 每块板的控制

- **电脑设置候选词**：分别设置 A/B 当前 Picker 词。文本必须非空、不含控制字符，最多 96 个 UTF-8 字节。设置后取消确认状态；本机 offer pending 时候选词锁定，需先完成或取消 offer。
- **编码器 − / ＋**：Picker 中浏览历史或生成新词；Sentence / Navigate 中循环移动 cursor。DeleteConfirm 中不动作，空句中不移动。
- **A 按钮**：Picker 第一次确认、第二次立即发起主动 offer；Sentence 中根据模式进入删除确认、确认删除、进入 InsertSide 或发起 pull。对方可接收时，Web API 会自动推进到 ACCEPT/DONE 完成，无需额外点击。
- **B 按钮**：Picker 刷新或取消确认；DeleteConfirm 取消删除；InsertSide 切换 BEFORE/AFTER；Navigate 清句并回 Picker；pending 时优先取消本机请求。

页面同时显示视图/模式、cursor/词数、确认状态、连接和 pending。pending 中的 `req`、对端、阶段和固定 `pos` 可用于核对操作顺序。句子最多 12 词，每项标注来源板。

### 3.3 即时发送与 pending 的边界

主动 offer 和定位 pull 在按钮动作产生后立即开始。对端空闲且可接收时，Web 会自动完成以下阶段，最终不保留 pending：

1. 发送 PULL（仅定位 pull）和 OFFER，接收板按位置入句。
2. 接收板返回 ACCEPT，提供板收到后才换候选词，再记录 DONE。

`pending` 表示“已经发起、尚未确认完成”，不是发送前的人为等待。断线、句子满 12 词、接收记录暂满或提供方已有冲突 offer 时，pending 才会保留。排除故障后点击“重试等待中的传输”，或通过会改变连接/按键状态的 API 触发自动重试。

## 4. 双机逐步示例

下面用 `alpha`、`beta` 作候选词。模拟器词库在完成后会确定性换词，因此示例在每次完成后显式设置下一词，便于观察。

### 4.1 A 主动 offer 给 B

1. 在 A 的输入框设置 `alpha`。B 的候选词可任意设置，它不会仅因连接就自动入句。
2. 点击“连接 A.R ↔ B.L”。
3. 在 A 第一次点击 **A 按钮**：只确认 `alpha`，尚未发送。
4. 在 A 第二次点击 **A 按钮**：立即发送无 `pos` 的 OFFER。B 可接收时，A/B 会自动完成 ACCEPT 和 DONE，无需再点其他按钮。
5. 完成后 B 从左口接收，在句首插入 `alpha`，进入 Sentence / Navigate，cursor 指向新词；日志保留完整传输过程。A 收到 ACCEPT 后才换词，B 的 `[alpha]` 保持不变。

同一固定拓扑下，A→B 的无 `pos` offer 默认到 B 的句首；B→A 则默认到 A 的句尾。这是接收入边规则，不是定位 pull 的 BEFORE/AFTER 规则。

### 4.2 B 把 A 的新词 pull 到 `alpha` 后面

1. 承接上例，把 A 的候选词设为 `beta`，保持连接。
2. B 在 Navigate 中用编码器选中 `alpha`。本例只有一个词，cursor=0。
3. 按 **A** 进入 InsertSide，页面显示对端当前候选 `beta`。
4. 默认是 `L:BEFORE`；按一次 **B** 切到 `R:AFTER`。
5. B 再按 **A**，立即以固定 `pos=1` 发起 PULL，并自动完成后续 OFFER/ACCEPT/DONE。
6. 完成后 B 的句子为 `[alpha,beta]` 并回到 Navigate；A 收到 ACCEPT 后才换词，双方均无 pending。

若第 4 步保持 BEFORE，则位置为 0，结果是 `[beta,alpha]`。空 Sentence 的 BEFORE/AFTER 都固定为位置 0。完整固件语义见[句子编辑器说明](sentence-editor.md)。

## 5. 删除、定位 pull、取消与断线

### 5.1 删除与清句

1. 删除单词前先断开连接；已连接时 Navigate 的 A 会进入 InsertSide，而不是删除确认。
2. 用编码器选中目标词，按 A 进入 DeleteConfirm；再按 A 才删除，按 B 则取消且句子不变。
3. 删除最后一个词后仍停留在 Sentence / Navigate，显示空句，不会自动回 Picker。重新连接后可在空句中直接 pull 首词。
4. Navigate 中按 B 会立即清空整句并回 Picker，没有第二次确认；接收去重记录仍保留。

### 5.2 pull 后移动 cursor

PULL 发出时已经固定 `pos`。pending 期间转动编码器可以改变当前 cursor，但不会重算这笔请求的位置；收词后 cursor 指向实际插入的新词。可据此检查“移动 cursor 不改变已发出请求”的行为。

### 5.3 取消 pending

- OFFER 或 PULL pending 时按本机 B，只取消本机等待，之后不会因该 pending 自动重试。
- 取消不是远程撤销：若对端已经入句，对端句子和 receipt 不回滚。
- 模拟器取消 PULL 后回到 Navigate。Picker 中转动编码器也会取消本机 OFFER 并换词。
- 若要继续传词，取消后需重新发起一笔操作。

### 5.4 断线与重连

1. 正常空闲传输会自动完成；要观察 pending，可在满句、冲突或断线条件下构造未完成请求。
2. 两块板已建立的句子不会清空，各自 pending 和固定位置保留；InsertSide 会退出到 Navigate。
3. 断线时点击“重试等待中的传输”，日志只说明等待，不会完成传输。
4. 重新连接时自动重试。接收记录使同一请求至多插入一次；若是在入句后、ACCEPT 前断线，重连只完成确认，不会重复插入。

这里只模拟运行中的断线恢复，不表示重启后续传，也不证明真实触点可以安全带电插拔。

## 6. HTTP API 简要说明

页面本身使用以下同源 JSON API，也可用 `curl` 或测试脚本直接调用。除 GET 外，请求须带 `Content-Type: application/json`；字段必须匹配，不能附加未定义字段。

| 方法与路径 | 请求体 | 作用 |
|---|---|---|
| `GET /api/state` | 无 | 读取完整状态 |
| `POST /api/candidate` | `{"board":"A","text":"alpha"}` | 设置 A 或 B 的候选词 |
| `POST /api/action` | `{"board":"A","action":"confirm"}` | 按 A；`refresh` 表示按 B；之后自动尝试完成 pending |
| `POST /api/action` | `{"board":"B","action":"encoder","steps":1}` | 转动编码器；`steps` 为 −100～100 的非零整数 |
| `POST /api/connection` | `{"connected":true}` | 连接或断开固定拓扑；连接后自动重试 |
| `POST /api/step` | `{}` | 手动推进一个传输阶段，仅用于异常 pending 的重试/观察 |
| `POST /api/reset` | `{}` | 重置模拟器 |

成功的 POST 与 `GET /api/state` 都返回完整状态。顶层包括 `connected`、`connection`、`revision`、`boards` 和 `logs`；每块板包括 `view`、`mode`、`candidate`、`confirmed`、`sentence`、`cursor`、`insert_side`、`pending` 等。句子项包含 `text` 和 `source`。

请求格式或值无效返回 400，JSON 媒体类型错误返回 415，状态冲突（例如 offer pending 时修改候选词）返回 409；错误响应为 `{"error":"..."}`。

## 7. 与真实固件的边界

- **确定性交互模型**：模拟状态转移、请求 ID、接收去重、容量和 pending，不执行真实 WLP2 帧编解码或链路状态机。
- **不是硬件或 UART 验证**：不运行 ESP32 固件，不访问串口，不验证真实 UART 时序、身份/套件配置、电气安全、N8R8 Flash/PSRAM、墨水屏刷新、休眠或持久化。
- **自动完成不等于真实时序**：Web 在可接收时立即推进整个应用层流程，用于贴近固件的即时操作；手动重试用于观察异常 pending。它不模拟实际重传定时、丢包概率或总线竞争。
- **中文输入有显示边界**：模拟器允许中文及其他合法 UTF-8 文本，浏览器也能显示；真实固件当前字体只保证 ASCII 字形和宽度，不能据此宣称实体屏支持中文。
- **测试不等于实板验收**：模拟器与 native 测试通过，都不代表真实双板已经验证。实板步骤和未验收项见[句子编辑器说明](sentence-editor.md)与[接线说明](wiring.md)。

## 8. 测试

从仓库根目录运行模拟器的 14 项模型与 HTTP 测试：

```sh
python3 -m unittest simulator.test_simulator -v
```

若本机已安装可选的 Node.js，可额外检查浏览器脚本语法：

```sh
node --check simulator/static/app.js
```
