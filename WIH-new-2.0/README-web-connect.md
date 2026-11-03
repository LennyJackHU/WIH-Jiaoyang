# Words in Hand · USB-to-UART 网页连接

当前固件支持 Picker 单词事件、网页读取整句，以及模块上长按 A 经确认后主动上传整句。通信使用 **UART0，115200 baud**；Pogo 的 Serial1/Serial2 保持独立。

## 当前硬件操作

| 模块操作 | 效果 |
| --- | --- |
| Picker 第一次短按 A | 选中当前词，并输出一次 `word_confirmed` 事件 |
| Picker 第二次短按 A | 联机模式执行原 Pogo 传词，不重复输出单词事件；单机模式改为本机入句 |
| Picker 短按 B | 已选中时取消，未选中时 shuffle |
| Picker 长按 A | 单机模式确认页，A 确认、B 取消 |
| Sentence 短按 A | 在下划线选择模式与竖线插入模式之间切换 |
| Sentence 短按 B / 长按 B | 删除当前词确认 / 清空整句确认 |
| 非空 Sentence 长按 A | 上传确认页；A 冻结并上传整句，B 取消 |

短按在松开时派发，不再有旧版 350ms 双击识别等待。原网页连接、单词上传和按钮读取整句逻辑不变；长按 A 主动上传只是独立的附加事件路径。

## 主动整句上传

1. 网页先连接模块并等待识别固件。
2. 模块处于非空 Sentence，长按 A。
3. 屏幕显示 `UPLOAD TO COMPUTER?`，按 A 确认或 B 取消。
4. 固件冻结确认时的句子，依次输出 `upload_begin`、`upload_word`、`upload_end`。
5. 网页校验 request、数量和全部有序词完整后，一次性追加整句；不完整序列不会追加半句。

`UPLOADING` 仅表示 UART 分帧开始，不是网页应用层 ACK。固件没有离线补发队列，断线后的半句不会自动续传。

网页仍保留「放上整个句子」按钮：它发送 `get_sentence`，固件用 `sentence_begin/sentence_word/sentence_end` 返回快照。主动上传和按钮读取若包含相同内容，网页会按两次操作追加；只想保留一份时先清空网页句子。

## 使用与烧录

包结构：

```text
firmware/       与仓库根项目同步的 N8R8 固件
web/            网页、串口脚本和 Python 服务
README-web-connect.md
changes.patch   历史补丁，不作为当前源码入口
```

1. 解压到纯英文目录。
2. 如需 AI 功能，把原有 `web/config.json` 复制到 `web`；包内不含真实密钥。仅串口功能可使用测试配置。
3. VS Code / PlatformIO 打开本包的 `firmware`，不是 `firstConnect`。
4. 断开网页串口并关闭串口监视器。核对 ESP32-S3-DevKitC-1 N8R8，用 USB-to-UART 接口烧录。只有明确要求烧录时才执行 upload。

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

在 Chrome/Edge 打开 `http://localhost:8000`，Ctrl+F5 刷新后连接设备。不要直接双击 HTML。串口、整句导入和词库刷新不调用 AI；AI 评价与故事功能需要有效服务配置。

## 协议示例

原有 `word_confirmed` 和 `get_sentence` 协议不变。长按 A 主动上传新增：

```text
WIH:{"type":"upload_begin","protocol":1,"device":"...","session":"...","request":"...","count":2}
WIH:{"type":"upload_word","protocol":1,"device":"...","session":"...","request":"...","count":2,"index":0,"word":"hello"}
WIH:{"type":"upload_word","protocol":1,"device":"...","session":"...","request":"...","count":2,"index":1,"word":"world"}
WIH:{"type":"upload_end","protocol":1,"device":"...","session":"...","request":"...","count":2}
```

固件仍约每 2 秒输出 `ready`，网页按原流程识别设备；长按 A 上传不增加额外握手。

## 验证与实板验收

从仓库根目录运行：

```sh
pio test -e native
node --test WIH-new-2.0/web/tests/serial-bridge.test.mjs
pio run
```

native 覆盖交互、UART 事件、快照和分帧；Node 测试覆盖网页解析、去重、断线和完整主动上传。软件测试不能替代实板 USB/Pogo、电气或真实浏览器验收，也未验证在线 AI。

建议实板顺序：原单词上传与按钮读取整句 → Picker 其他按钮 → 单机本机入句 → Pogo 组句与竖线定位 → 网页按原流程连接 → Sentence 长按 A 确认上传 → 检查网页完整顺序 → 拔线验证不追加半句 → 面板休眠后重试。
