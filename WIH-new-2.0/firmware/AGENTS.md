# 项目工作约束

## 硬件与 PlatformIO 环境

- 所有实体控制器固定为 **ESP32-S3-DevKitC-1 N8R8**：8 MB QIO Flash + 8 MB OPI PSRAM。GPIO35–37 专用于存储器，不可分配给外设。
- 根 `platformio.ini` 的唯一固件环境和默认环境是 `esp32-s3-devkitc-1-n8r8`，配置固定为 `qio_opi`、QIO Flash、OPI PSRAM、8 MB 分区和 `BOARD_HAS_PSRAM`。不要无要求地增加其他存储容量/模式环境或放宽这些选项。
- 默认 `pio run`、`pio run -t upload` 和 `pio upload` 只针对上述 N8R8 固件；`native` 仅用于主机测试，不生成可烧录固件。烧录属于真实设备操作，只有在用户明确要求时执行。
- 测试或编译通过不等于实板、电气或真实 UART 验收通过。文档不得把模拟器结果写成实板验证结果。

## 常用命令

从仓库根目录运行：

```sh
# 49 项固件核心 native 测试
pio test -e native

# 14 项 Web 模拟器模型与 HTTP 测试；仅需 Python 3 标准库
python3 -m unittest simulator.test_simulator -v

# 可选：仅在安装 Node.js 时检查 JS 语法
node --check simulator/static/app.js

# 默认 N8R8 固件构建
pio run
```

模拟器启动与用法见 `docs/simulator.md`；不要在不需要长驻服务时启动它。

## 本机 PlatformIO “Processing”排查

本机 shell 可能没有全局 `pio`，但 PlatformIO 虚拟环境入口可用。先确认实际入口，不要假设全局 Python 与 PlatformIO 虚拟环境相同：

```sh
command -v pio
~/.platformio/penv/bin/pio --version
~/.platformio/penv/bin/pio system info
```

若 `pio` 不在 PATH，以下命令可将上述入口替换为 `pio`。已在本机验证该入口能运行 native 测试并解析两个环境的包：

```sh
~/.platformio/penv/bin/pio pkg list -e native
~/.platformio/penv/bin/pio pkg list -e esp32-s3-devkitc-1-n8r8
```

如果构建或测试停在 `Processing`：

1. 先中断任务，用同一实际入口加 `-v` 重跑，保留完整输出，判断停在依赖解析、包安装、构建还是测试收集；不要把没有测试汇总当作通过。
2. 用 `system info` 核对 Core、Python 和 PlatformIO Core Directory，用 `pkg list -e ...` 单独检查包解析。检查是否还有其他 PlatformIO 构建/测试任务同时操作本项目。
3. 包列表出现多个版本不自动等于损坏，应以目标环境的依赖解析和后续错误为准。不要仅凭 `Processing` 就删除 `.pio`、`~/.platformio` 或全局包，也不要擅自修改代理、镜像、依赖版本或 `platformio.ini`。
4. 只有实际执行并验证过的修复才能记录为 workaround；未验证时只保留诊断命令和原始错误，交由用户决定清理或变更环境。

本机已验证：若依赖已经安装且 `pio run` 仍停在包管理前置步骤，可跳过 `install_project_env_dependencies`，直接调用同一 PlatformIO/SCons 构建入口。此命令不会跳过编译、链接或固件生成：

```sh
"$HOME/.platformio/penv/bin/python" -c 'from platformio.project.config import ProjectConfig; from platformio.platform.factory import PlatformFactory; c=ProjectConfig.get_instance("platformio.ini"); p=PlatformFactory.from_env("esp32-s3-devkitc-1-n8r8", targets=[], autoinstall=False); v={"pioenv":"esp32-s3-devkitc-1-n8r8","project_config":c.path,"program_args":[]}; r=p.run(v, [], False, False, 10); raise SystemExit(r["returncode"])'
```

烧录时使用相同调用，将 `targets=[]` 改为 `targets=["upload"]`，向 `v` 增加 `"upload_port":"/dev/cu.usbserial-10"`，并将 `p.run` 的 targets 改为 `["upload"]`。端口必须先通过 `pio device list` 确认，不能照搬示例。

## 实板启动与大状态结构

- WIH4 的 `PersistState` 包含有序句子、来源和 receipts，体积达数千字节。不要在 Arduino 主任务栈上叠加多个此类副本；启动加载和 `saveAll()` 使用静态缓冲区，保存前逐次清零。
- 若启动在 `psram: found=1` 后出现 `Double exception`，先用本机 `xtensa-esp32s3-elf-addr2line` 解码 `.pio/build/esp32-s3-devkitc-1-n8r8/firmware.elf`，不要仅凭 native 测试通过判断实板安全。
- 当前分支已实测单板启动、WIH3→WIH4 迁移、N8R8 PSRAM、屏幕刷新和休眠前保存；这不等于双板 UART 或全部句子交互已验收。

## 持续监听与双板 SOP

- 无操作约 5 秒后只允许墨水屏 hibernate，不得调用 `esp_deep_sleep_start()`。MCU、UART、`game.tick()` 和周期 `alive` 日志必须持续运行。
- 屏幕已休眠且 `dirty=false` 时直接返回，避免反复 `saveAll()` 写 NVS。连接、来词和本地输入要刷新空闲计时；下一次重绘先全刷，传输不等待屏幕恢复。
- 修改→49 项 native+14 项 Web 测试→N8R8 构建→实板超过 5 秒持续监听验证→提交→推送→PR→另一块板同步提交并烧录→Pogo 验收，完整步骤见 `docs/hardware-sop.md`。
- 不再配置 member/recorder 白名单。相同 `WIH_KIT_ID` 的陌生模块开放加入；HELLO v3 携带 MAC 指纹，冲突双方随机换号并通过 NVS `wih/node_id` 持久化，详见 `docs/open-join.md`。
- 排查 `configured/identity/links/uartErr/reassigned`、版本、接线和面板休眠时，按 `docs/troubleshooting.md` 的顺序收集双方日志，不要凭单一 `links=0` 或非零历史错误计数猜测原因。
