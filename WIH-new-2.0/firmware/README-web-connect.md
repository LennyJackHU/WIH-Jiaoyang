# Words in Hand · patch-3 硬件确认上传版

本次修正：单词由**模块 A 按钮确认后自动传到网页**，不再通过网页按钮主动读取单词。网页保留「放上整个句子」与「换一组词块」。

## 基础版本与保留内容

基于 `https://github.com/LennyJackHU/WIH-Jiaoyang/tree/rogich-yuan-patch-3`，提交 `aef98b60c906cdae103bb07055861de7fa53c9de`。

原纸片屏幕、旋钮、A/B 的短按/长按/双击/组合操作、Pogo 收发、句子编辑与存储均保留。`display`、`input`、`word_game`、`sentence`、`config`、`wlp2`、`storage` 原逻辑源文件与基线一致。

`main.cpp` 仅接入 UART0 网页服务，并在原 `game.pressConfirm(now)` 执行之后观察确认状态。不会再调用一次确认，不替代硬件按键行为。在原 A 处理分支中去掉新增的 observeConfirm 行后，完整 handleButtons 与基线逐字节一致。

## 当前操作

| 操作 | 网页效果 |
| --- | --- |
| 先在网页连接设备，再在模块选词页旋转旋钮 | 不上传，只改变候选词 |
| 模块选词页从未确认状态短按 A 确认 | 自动追加一个对应词块 |
| 已确认后再次短按 A | 保留原 Pogo 传词行为，不重复追加到网页 |
| B 取消后，再短按 A 确认同一个词 | 作为新的确认操作，再追加一个词块 |
| 长按/双击、句子编辑、开机恢复和 Pogo 收到词 | 不冒充新的本机选词确认事件 |
| 网页「放上整个句子」 | 按顺序追加 USB 所连板保存的完整句子 |
| 网页「↻ 换一组词块」 | 更换下方 30 个随机词，保留网页已经拼好的句子 |

网页原先用于读取单词的绿色气泡按钮改为非交互提示：「按模块 A 放上词块」，并显示最近收到的单词。灰蓝气泡仍是「放上整个句子」按钮。下方手动输入功能也保留。

**请单击 A，不要快速双击。** patch-3 会先等待原有的双击识别窗口（约 350ms），再派发单击确认事件；这是原输入逻辑。如果开机恢复的是已确认词，先按 B 取消，再按 A，才能产生一次新的确认上传。

固件没有离线补发队列；未连接时的确认不保证保留。连接时不会主动补发已确认词或 NVS 恢复内容，请连接后再进行新的确认。

整句最多 12 词，按 `game.sentence()` 保存的顺序导入；不额外加入隐藏候选词。patch-3 编辑动作本身可能将候选词加入句子，这部分保留原行为。网页每次导入整句都追加；如果它包含已逐词上传的词，会再次出现。只想显示整句时，先点「清空重来」。

## 使用与烧录

本包结构：

```text
firmware/       patch-3 固件与网页串口输出
web/            网页、串口脚本和 Python 服务
README-web-connect.md
changes.patch   可选：以 patch-3 GitHub 仓库根目录为基准的补丁
```

1. 解压到纯英文目录，例如 `E:\WIH-Patch3-Web`。
2. 把你原有的 `web\config.json` 复制到新包的 `web` 下；包内不含真实密钥。
3. VS Code / PlatformIO 打开本包的 `firmware`，不是 firstConnect。
4. 断开网页串口并关闭串口监视器。核对板型是 ESP32-S3-DevKitC-1 N8R8，用 USB-to-UART 接口重新烧录。

PowerShell 示例（COM9 必须换成实际端口）：

```powershell
cd E:\WIH-Patch3-Web\firmware
$env:PLATFORMIO_BUILD_DIR = 'E:\WIH-Patch3-build'
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" device list
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e esp32-s3-devkitc-1-n8r8 -t upload --upload-port COM9
```

启动网页：

```powershell
cd E:\WIH-Patch3-Web\web
python server.py 8000
```

在 Chrome/Edge 打开 `http://localhost:8000`，Ctrl+F5 刷新，先连接设备，再操作模块按钮。不要直接双击 HTML。网页使用 UART0，115200 baud；Pogo 的 Serial1/Serial2 保持原样。

现有 Python 服务仍要求 config.json 中 apiKey/baseUrl/model 非空；仅测串口可复制 config.example.json 为 config.json，并把 apiKey 设为 test-only。串口、整句导入和词库刷新不调用 AI；AI 评价与故事功能需要有效服务配置。

## 修改范围与协议

- 新增 `src/web_serial.h/.cpp`：首次确认上传 `word_confirmed`；网页整句命令 `get_sentence` 返回冻结的句子快照。
- `src/main.cpp`：新增网页服务初始化、主循环/BUSY 回调轮询；A 的原业务操作之后新增只读观察。
- `platformio.ini`：仅 native 源文件清单增加 web_serial.cpp，硬件配置不变。
- `web/index.html`：自动接收单词并追加，绿色提示气泡、整句按钮、随机词库刷新；原页面、手动输入、AI 和社群保留。
- `web/serial-bridge.js`：按 device/session/seq 去重单词事件；按 request 匹配整句，完整收到才追加，超时/断线不追加半句。已移除网页 get_word 读取操作。
- 测试：首次/再次确认、取消后重确认、句子编辑不误上传、Pogo 收词后导出、快照/容量/缓冲、断线/超时、DOM 自动接收和词库刷新。

示例自动事件：

```text
WIH:{"type":"word_confirmed","protocol":1,"device":"00000000000000f1","session":"00000000000000a1","seq":1,"word":"hello"}
```

此事件仅由首次本机确认产生，网页不发送单词读取命令。整句仍由网页发 `get_sentence`，固件分 `sentence_begin` / `sentence_word` / `sentence_end` 返回。

## 验证与实板验收

- PlatformIO native：62/62 通过（49 项基线 + 13 项网页事件/整句测试）。
- 网页串口模拟：28/28 通过；实际 HTML 的 jsdom 集成测试：1/1 通过。
- 原隔离 HTTP 7 项和模拟器 14 项在上一轮已通过，本次未修改服务或模拟器。
- ESP32-S3 N8R8 目标编译通过：RAM 73,460 / 327,680 字节（22.4%），Flash 应用分区 403,189 / 3,342,336 字节（12.1%）。未执行 Upload。
- 硬件核心源文件原样对照、网页脚本语法和补丁适用性检查通过。
- 未完成实板 USB/Pogo 电气与真实浏览器验收；软件测试不能替代实板验证。未验证在线 AI。

建议实板检查：正常纸片 UI/按键 → 网页连接 → B 取消已确认状态 → A 单击上传一词 → 再次 A 仍是 Pogo 传词且网页不重复 → Pogo 组句 → 网页整句导入 → 换一组保留原句 → 面板休眠后再选词确认 → 拔线重连。
