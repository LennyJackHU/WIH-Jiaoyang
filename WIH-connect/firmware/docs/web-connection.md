# Words in Hand 网页连接版

更新日期：2026-11-01。

## 本次完成了什么

基于 `WIH-Jiaoyang-main (1).zip` 的完整固件新增网页事件输出，同时接入现有 `index.html` 和 `serial-bridge.js`。保留现有 `server.py`，不增加 Python 串口依赖，不读取或修改原有 API 密钥配置。

**使用 USB-to-UART 串口，不是 Wi-Fi，也不是 ESP32-S3 原生 USB CDC。** 网页经浏览器 Web Serial 直接接收单词；Python 服务负责提供网页及原有 AI 接口。板子的 Serial1/Serial2 Pogo 通信继续使用原有 WLP2。

仅完成源码修改、自动测试和 N8R8 目标编译；**没有烧录、实板联调、真实浏览器 Web Serial 验收或在线 AI 调用验证**。

## 文件位置

在原工作文件夹中：

```text
WIH\
  index.html
  serial-bridge.js
  server.py
  config.example.json
  config.json                         原有私密配置，保留原样
  tests\                              网页自动测试
  使用说明.txt
  WIH-网页连接版\
    WIH-Jiaoyang-main\                 本版固件工程
    网页原文件备份\                    原 index.html、串口脚本和使用说明
```

交付包 `WIH-网页连接完整版.zip` 解压后：

```text
firmware\                             本版固件工程、文档、模拟器和测试
web\                                  网页、Python 服务、示例配置和测试
使用说明.txt
```

压缩包不包含真实 `config.json`、`.pio`、`.history`、`__pycache__`、`.git`、原网页备份或旧版嵌套 ZIP。固件使用本工程根目录的 `platformio.ini`，不要使用 `firstConnect` 子工程，也不要用旧的 `ESP32-S3固件.zip` 替代。

## 1. 准备固件

硬件仍固定为 **ESP32-S3-DevKitC-1 N8R8**：8 MB QIO Flash + 8 MB OPI PSRAM。默认环境 `esp32-s3-devkitc-1-n8r8`、`qio_opi`、8 MB 分区及 `BOARD_HAS_PSRAM` 均未改变；GPIO35–37 仍保留给存储器。

从固件工程目录运行，只编译、不烧录：

```powershell
pio run
```

本机 PlatformIO 的实际入口为：

```powershell
& 'C:\Users\97039\.platformio\penv\Scripts\pio.exe' run
```

### Windows 中文路径

建议将完整包解压到纯英文路径，例如 `E:\WIH-Web`。本机在默认中文构建目录遇到链接器 `cannot open map file ... Invalid argument`；改用有写入权限的 ASCII 构建目录后，同一固件工程构建通过，不需要修改 `platformio.ini`：

```powershell
# 示例路径；请使用自己有写入权限的纯英文目录。
$env:PLATFORMIO_BUILD_DIR = 'E:\WIH-build'
pio run
```

本次实际验证使用 Codex 提供的可写 ASCII 可视化目录下的 `wih-build`。环境变量只影响当前 PowerShell 及其子进程；在其他终端操作时需重新设置。若后续决定烧录，应使用同一工程和一致的构建目录设置。

烧录必须由你明确决定，并先核对板型和真实 COM 端口；烧录前断开网页串口并关闭串口监视器。本次没有执行任何 upload 命令，也未提供可误认为完整烧录镜像的单个 `firmware.bin`。

## 2. 启动网页

在原工作文件夹中运行；使用完整包时先进入 `web` 目录。

1. 已有 `config.json` 时保留原文件，**不要覆盖**。仅全新部署时，将 `config.example.json` 复制为 `config.json`，填写自己的服务参数。
2. 原 `server.py` 启动时要求 `apiKey`、`baseUrl`、`model` 非空。只测串口且不调用 AI 时，可在新部署中使用非空测试占位参数；AI 功能则必须配置有效服务及密钥。
3. 用 Python 3 启动：

   ```powershell
   python server.py 8000
   ```

4. 使用桌面 Chrome 或 Edge 打开 `http://localhost:8000`。不要直接双击 HTML 使用 `file://`。此服务是本地开发服务，不要暴露到公网；本次不是服务安全审计。
5. 用支持数据传输的 USB 线连接板子的 **USB-to-UART 接口**。本版保留 `ARDUINO_USB_CDC_ON_BOOT=0`，不要误用原生 USB 接口。
6. 关闭占用该 COM 口的 PlatformIO/Arduino 串口监视器、其他串口工具或网页。
7. 点击网页「连接设备」，在浏览器选择器中选择该 USB-to-UART COM 口。每次重连都会重新显示选择器，可纠正误选的端口。
8. 网页以 **115200 baud** 打开串口，先显示「串口已打开，等待词块固件…」。收到有效 `ready` 或单词事件后才显示「已连接」。固件启动完成后每 2 秒发送一次 `ready`，因此不需要为了网页重新启动板子。

网页尝试将 DTR/RTS 置为 false；驱动不支持设置时会继续读取。串口打开是否导致硬件复位需在实际板子上验证。

> 「已连接」表示当前串口会话曾识别到本协议，不是持续健康保证。本版没有 ready 超时看门狗；板子卡死但串口未关闭时，状态可能仍显示已连接。拔线或读流出错后会提示中断，需要手动重新连接。

## 3. 按键与网页行为

| 板上操作 | 网页结果 |
| --- | --- |
| Picker 页旋转编码器选词 | 不追加 |
| Picker 页第一次按 A，从未确认变为已确认 | 追加一个词块；首页会自动进入拼贴台 |
| 对已确认词再次按 A | 仍按原规则向唯一直接邻居传词，不再次追加 |
| 按 B 取消确认 | 不撤回网页已追加的词，请在网页单独删除 |
| B 取消后再次 A 确认同一个词 | 视为新操作，再追加一个词块 |
| Sentence 页删除、定位拉取等操作 | 不产生本机选词事件 |
| 从另一块板接收到词 | 不逐词导入网页 |
| NVS 开机恢复已确认词或句子 | 不补发网页事件 |

在拼贴台以外的其他页面收到词时更新网页句子，但不打断当前阅读。现有缪斯间、故事、学习和诗集墙逻辑不改写。

### 明确边界

- 单向「本机确认单词 → 网页」，不回写板子，不同步整句；网页句子和板上句子独立。
- 未连接时的单词不排队补发；电脑或浏览器离线期间的事件不保证保留。
- 一个网页实例一次监听一块板、一个串口。连接一块板不会自动汇总其他板子的确认操作。
- 新协议按「设备指纹 + 启动会话 + 序号」去重，同一页面跨断线重连保留最近 1024 个事件；刷新页面会清空去重记录。新启动会话的 `seq=1` 不会被旧会话误判为重复。
- 原双板 WLP2、开放加入、冲突换号持久化、句子编辑和静态大状态缓冲保持不变。面板约 5 秒休眠后，MCU、UART 和事件轮询仍继续运行。
- 串口与 AI 是独立链路；串口连通不代表 AI 配置、网络或模型服务可用。

## 4. 串口协议

普通调试日志保持输出。网页仅处理以 `WIH:` 开头、以换行结束的 JSON 行：

```text
WIH:{"type":"ready","protocol":1,"device":"00000000000000f1","session":"00000000000000a1"}
WIH:{"type":"word_confirmed","protocol":1,"device":"00000000000000f1","session":"00000000000000a1","word":"hello","seq":1}
```

- `device`：现有 WLP2 MAC 设备指纹，16 位十六进制文本，不使用可能在冲突后变化的节点 ID。
- `session`：现有 WLP2 启动标识，16 位十六进制文本，区分不同开机会话。
- `seq`：本次启动中单词事件的 32 位序号，从 1 开始；`ready` 不占用序号。
- `word`：使用固件现有文本校验和 JSON 转义，最多 96 个 UTF-8 字节，不接受控制字符。
- 网页使用流式 UTF-8 解码，支持跨串口分片；忽略普通日志、损坏 JSON、未知协议版本和超长行。
- 兼容旧版无 `protocol` 的 `word_confirmed` 行；旧版只按相邻序号做有限去重，不具备新协议完整会话识别能力。建议两端使用本次版本。
- 这不是认证协议；选择串口相当于信任该串口的数据。

## 5. 修改清单

固件：

- 新增 `src/web_serial.h`、`src/web_serial.cpp`：事件序列化、首次 A 确认检测、启动及周期 `ready`。
- 修改 `src/main.cpp`：把 A 按钮交给事件包装器，再执行原 `WordGame` 操作；主循环及显示 busy 回调持续轮询；UART0 增加发送缓冲，不调用 `Serial.flush()` 等待整帧排空。
- 修改 `platformio.ini`：仅把 `web_serial.cpp` 加入 native 测试源列表，硬件配置不变。
- 新增 `test/test_native/web_serial_tests.cpp`，在 `test_main.cpp` 注册 9 项测试。
- 更新本说明与 README。原 `AGENTS.md` 保持不变，其中 49 项是本次修改前的历史计数。

网页：

- `serial-bridge.js`：状态识别、协议解析、有限去重、断线处理、读锁释放及重连。
- `index.html`：连接按钮、状态和单词事件接入；保留其余界面和业务逻辑。
- 新增 `tests/serial-bridge.test.mjs` 与 `tests/test_web_server.py`。
- `server.py` 不修改；真实 `config.json` 不读取、不修改、不打包。

## 软件验证结果

2026-11-01 软件验证：

| 项目 | 结果与限制 |
| --- | --- |
| C++ 核心及网页事件测试 | 58/58 通过；本机用 MSVC 编译并执行同一组测试源码 |
| `pio test -e native` | 本机缺少 gcc/g++，此命令未完成，不能报告为通过 |
| JavaScript 串口自动测试 | 20/20 通过；使用模拟串口流，非真实设备 |
| 隔离 HTTP 测试 | 7/7 通过；只复制必要网页文件到临时目录，使用虚拟配置并禁止上游 API 调用 |
| 原 Python 双板模拟器测试 | 14/14 通过；一次运行遇到 Windows 连接中断，立即重跑全部通过 |
| JavaScript 语法 | 网页两个内嵌脚本、串口模块、模拟器脚本全部通过 |
| N8R8 `pio run` | 编译通过；RAM 71,876 / 327,680 字节（21.9%）；Flash 385,561 / 3,342,336 字节（11.5%），后者为应用分区容量 |
| 实板、真实串口、在线 AI | 未执行 |

固件目录中的常规复测命令：

```powershell
pio test -e native
python -m unittest simulator.test_simulator -v
node --check simulator/static/app.js
pio run
```

网页所在目录（原工作目录或完整包的 `web` 目录）：

```powershell
node --test tests/serial-bridge.test.mjs
python -m unittest discover -s tests -p test_web_server.py -v
```

HTTP 测试不会使用你已有的配置和密钥。Node 测试用 ES 模块加载串口脚本，不需要更改网页的包配置。

本机缺少 gcc/g++ 时，MSVC 替代验证使用 `src/wlp2_json.cpp`、`src/wlp2_protocol.cpp`、`src/picker.cpp`、`src/words.cpp`、`src/word_game.cpp`、`src/sentence.cpp`、`src/web_serial.cpp`，加上 `test/test_native` 的四个测试文件与 Unity 的 `unity.c`，编译选项为 `/EHsc /utf-8 /D_CRT_SECURE_NO_WARNINGS`。该替代验证不代表 PlatformIO native 工具链安装成功。

## 6. 烧录后的实板验收清单

以下需在你决定烧录后执行，不能用上表的软件结果替代：

- [ ] 核对 N8R8、正确接口和 COM 端口；烧录后检查正常启动、`psram: found=1` 及原界面。
- [ ] 先用 115200 串口监视器确认有周期 `WIH:...ready`，然后**关闭监视器**，再由网页连接同一端口。
- [ ] 网页从等待状态变为已连接；Picker 第一次 A 只追加一个正确词块，第二次 A 不重复追加。
- [ ] B 取消不撤回；重新确认再追加一次；网页其他页面不会被无故跳转。
- [ ] 空闲超过 5 秒、墨水屏休眠后，串口仍有 ready；本地操作和原双板接收仍能正常进行。
- [ ] 拔线出现中断，重连能继续接收；板子重启后的首个新确认不会被旧序号过滤。
- [ ] 分别确认 Sentence 页、板间收词及 NVS 恢复不会被当作新确认导入。
- [ ] 两板运行同版固件，按原 `hardware-sop.md` 和 `sentence-editor.md` 验收 Pogo 双向通信、开放加入、冲突恢复、定位拉取、删除和持续监听。
- [ ] 最后单独验证网页的 AI 功能，检查自己的配置和网络；不要在截图或日志中分享密钥。

## 7. 常见问题

| 现象 | 排查 |
| --- | --- |
| 找不到 COM | 检查数据线、USB-to-UART 接口和驱动；不要先改协议或板型 |
| 连接失败或端口占用 | 关闭串口监视器、另一网页及其他串口程序，再重试 |
| 一直等待词块固件 | 等待启动完成；核对端口、接口以及是否烧录了本次工程 |
| 已连接但不加词 | 核对是否在 Picker 第一次按 A；Sentence 页 A 不产生该事件 |
| 网页服务无法启动 | 检查 Python、目录、端口占用以及配置的三个必填字段 |
| AI 报错但串口正常 | 分别检查 AI 服务配置与网络，不需要因此修改串口代码 |
| 中文路径链接失败 | 使用前述 ASCII 构建目录；不要无根据删除 PlatformIO 缓存 |

其他电气、休眠和板间问题仍按 [troubleshooting.md](troubleshooting.md) 收集双方日志。
