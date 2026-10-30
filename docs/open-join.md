# WLP2 开放加入与节点 ID 冲突恢复

当前固件不要求预注册成员。物理连接的两个模块只要使用相同 `WIH_KIT_ID`、相反 L/R 端口和正确的 WLP2 v3 帧，就可以交换身份、建立链路并传输应用数据。现场日志分析和逐项排查见 [通信、休眠与双板排查手册](troubleshooting.md)。

## 1. 身份字段

每块板在启动时生成或恢复以下字段：

| 字段 | 长度 | 来源与用途 |
|---|---|---|
| `id` | 16 位十六进制，64 bit | 首次由 eFuse MAC 派生；发生冲突后使用硬件随机数重新生成并保存到 NVS |
| `boot` | 16 位十六进制，64 bit | 每次启动或换号时随机生成，区分同一节点的不同运行会话 |
| `fp` / device fingerprint | 16 位十六进制，64 bit | 固定由 eFuse MAC 派生，只用于区分相同节点 ID 是否来自不同物理设备 |
| `kit` | 32 位十六进制 | 共享应用标识；不同 kit 不建立连接 |

不再使用 `WIH_MEMBER_x_ID`、member 白名单、recorder 白名单或角色匹配。`src/config.h` 只保留共享 `WIH_KIT_ID` 作为身份配置。

## 2. 开放握手

HELLO 和 HELLO_ACK 使用协议版本 `v=3`，携带 `id/boot/nonce/kit/fp/port`，ACK 另带 `echo`。接收方仍校验 JSON、CRC、字段长度、端口对应关系和 kit；不符合协议格式的数据不会进入应用层。

```text
陌生模块接触
→ 交换 id / boot / fp / kit / port / nonce
→ kit 和端口有效，且 id 不冲突：建立 UP 链路和 sid
→ NODE_INFO 学习当前候选词与邻接关系
→ 允许 OFFER / PULL / ACCEPT / DONE
```

节点只直接识别实际连接的邻居，不需要提前知道全网名单。三块链式加入使用同一机制；应用层 WORD_OFFER/PULL 仍只允许直接邻居，不能借开放加入绕过直接邻居校验。

## 3. 同设备重启与真实冲突

- 同一物理设备重启：`id` 和 `fp` 不变，`boot` 更新，不触发换号。
- 不同物理设备使用相同 `id`：`fp` 不同，判定为真实 ID 冲突。
- 收到与自身 `id` 和 `fp` 都相同的 HELLO：视为自身回声或无法区分的同一设备，不建立连接。
- 不同 kit 的帧直接忽略，不触发换号。

## 4. 冲突换号闭环

检测到“同 ID、不同指纹”后，双方分别：

1. 使用 `esp_random()`（native 测试使用可控随机函数）生成新的 64 bit 节点 ID。
2. 避开旧冲突 ID 和当前已知的邻居 ID；同时生成新的 boot ID 和链路 nonce。
3. 清除旧链路、网络去重记录和未完成接收队列，并按随机退避重新发送 HELLO。
4. 通过 `IdChangedFn` 回调把新 ID 写入 NVS 的 `wih/node_id`。
5. 重启后优先读取 `node_id`；没有有效保存值时才回退到 MAC 派生 ID。
6. 如果随机新 ID 再次冲突，重复检测、换号和重握，而不是只依赖随机概率。

启动日志 `wlp2: reassigned id=<新ID> saved=1` 表示换号及 NVS 保存成功。`saved=0` 表示持久化失败，当前运行仍使用新 ID，但重启后可能回退，需要检查 NVS。

## 5. 安全与协议边界

- 开放加入意味着任何持有相同固件和 kit 的模块都能在物理接触后加入并传词；本功能不提供成员准入控制或密码学身份认证。
- MAC 指纹不是密钥，不能防止伪造；它只解决低概率 ID 碰撞和同一物理设备识别。
- 当前没有配对时间窗。若以后需要“仅开机 30 秒”或“按住 B 才接受陌生模块”，应在链路 UP 前增加明确配对状态，不要重新引入静态 member 白名单。
- WLP2 v3 与旧 v2 白名单固件不兼容，两块板必须同步到同一提交后再验收。

## 6. 测试

`pio test -e native` 当前共 49 项，新增覆盖：

- `test_unlisted_modules_join`：未登记 ID 的陌生模块直接加入。
- `test_same_device_restart_keeps_id`：同设备换 boot 不换号、不触发持久化回调。
- `test_id_conflict_reassigns_persists_and_recovers`：双方换号、回调保存的新 ID 与运行 ID 一致、重新握手成功。
- `test_offer_and_pull_after_id_conflict_recovery`：冲突恢复后完成主动 OFFER 和定位 PULL。
- 既有三块链式转发测试继续覆盖开放加入后的短链行为。

native 的回调测试验证换号持久化机制，不等同于 ESP32 NVS 实板写入验证；实板还需检查 `saved=1` 和重启后的 ID 保持。
