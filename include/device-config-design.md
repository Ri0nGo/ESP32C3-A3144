# 设备配置设计说明

本文档定义 ESP32C3-A3144 固件的设备配置协议。当前设备定位为 A3144 霍尔门磁开关采集器，典型场景是冰箱门开关检测。目标是让同一份固件可以烧录到不同设备，设备身份、硬件描述、网络参数、运行角色和 A3144 开关采集参数通过串口写入 ESP32 NVS 持久化存储。

## 设计目标

- 固件不写死 `PRODUCT_KEY`、`DEVICE_KEY`、WiFi、MQTT 和硬件描述参数。
- 设备首次烧录后通过串口写入配置。
- 配置写入 NVS，设备重启后继续生效。
- MQTT topic 前缀由代码根据 `product_key` 和 `device_key` 自动拼接。
- 不要求外部传入 `mqtt_topic_prefix`。
- `hello` 只用于确认串口通信可用，不返回设备信息。
- 设备配置和硬件信息通过 `get_config` 返回。
- A3144 GPIO、有效电平、去抖、定时补报间隔、上传字段名和值映射通过 `sensor` 分组配置。
- 数据上报只保留业务必要字段，例如 `door=0` 或 `door=1`。
- 不上报 `rpm`、`count`、`delta`、`level`、`magnet` 等调试或脉冲统计字段。

## 传感器模型

A3144 是数字霍尔开关，不输出模拟磁场强度。当前固件只把它作为门磁开关使用：

- GPIO 当前电平用于判断磁铁是否靠近。
- `sensor.active_level` 表示检测到磁铁时的有效电平。
- `sensor.active_value` 表示检测到磁铁时上传的业务值。
- 未检测到磁铁时上传 `active_value` 的相反值。
- 默认业务字段名是 `door`。
- 状态变化时立即上报，同时按 `sensor.report_interval_ms` 定时补报当前状态。

冰箱门默认安装约定：

- 门关闭时，磁铁靠近 A3144。
- A3144 检测到磁铁时输出低电平。
- 默认上传 `door=0` 表示关门。
- 磁铁离开时上传 `door=1` 表示开门。

默认约定：

| 项 | 默认值 | 说明 |
| --- | --- | --- |
| 传感器类型 | `a3144` | 当前只支持 A3144 数字霍尔开关。 |
| 信号引脚 | `GPIO4` | 可通过 `sensor.pin` 修改。 |
| 有效电平 | `low` | 常见 A3144 模块检测到磁铁时输出低电平。 |
| 去抖时间 | `50 ms` | 门磁开关状态变化去抖。 |
| 定时补报间隔 | `60000 ms` | 即使状态未变化，也定时补报当前门磁状态。 |
| 上传字段名 | `door` | 上报 payload 中 `data` 下的业务字段。 |
| 有效值 | `0` | 检测到磁铁时上传的值。未检测到磁铁时上传 `1`。 |

## 角色定义

`device_role` 用于决定设备运行模式。

| 枚举值 | 说明 |
| --- | --- |
| `standalone` | 独立节点。设备自己连接 WiFi 和 MQTTS，检测门磁状态后直接上传到 MQTT。 |
| `espnow_slave` | ESP-NOW 从节点。设备不连接 WiFi/MQTT，只通过 ESP-NOW 将门磁状态发送给主节点。 |

## 配置结构

配置根字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `product_key` | string | 产品 Key，用于拼接 MQTT topic。 |
| `device_key` | string | 设备 Key，用于上报 payload 的 `key` 字段，也作为 MQTT client id。 |
| `device_role` | string | 设备角色，可选 `standalone` 或 `espnow_slave`。 |
| `device` | object | 设备信息。`name`、`version` 可写入；`chip`、`mac`、`flash` 由设备运行时读取，只在 `get_config` 返回。 |
| `sensor` | object | A3144 门磁开关采集参数。 |
| `wifi` | object | WiFi 配置，仅 `standalone` 使用。 |
| `mqtt` | object | MQTTS 配置，仅 `standalone` 使用。 |
| `espnow` | object | ESP-NOW 配置，仅 `espnow_slave` 使用。 |

`device` 字段：

| 字段 | 类型 | 来源 | 说明 |
| --- | --- | --- | --- |
| `name` | string | 配置写入 | 当前硬件名称。 |
| `version` | string | 配置写入 | 当前硬件版本号。 |
| `chip` | string | 运行时读取 | 芯片型号，例如 `ESP32-C3`。只在 `get_config` 返回。 |
| `mac` | string | 运行时读取 | WiFi MAC 地址。只在 `get_config` 返回。 |
| `flash` | number | 运行时读取 | Flash 容量，单位字节。只在 `get_config` 返回。 |

`sensor` 字段：

| 字段 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `type` | string | `a3144` | 传感器类型，当前只支持 `a3144`。 |
| `pin` | number | `4` | A3144 OUT 信号接入的 ESP32-C3 GPIO，默认 `GPIO4`。 |
| `active_level` | string | `low` | 检测到磁铁时的有效电平，可选 `low` 或 `high`。 |
| `debounce_ms` | number | `50` | 开关状态去抖时间，单位毫秒。 |
| `report_interval_ms` | number | `60000` | 当前状态定时补报间隔，单位毫秒。传 `0` 表示只在状态变化时上报。 |
| `data_key` | string | `door` | 上报 payload 中 `data` 下的字段名。 |
| `active_value` | number | `0` | 检测到磁铁时上传的值，只允许 `0` 或 `1`。 |

`wifi` 字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `ssid` | string | WiFi SSID，仅 `standalone` 必需。 |
| `password` | string | WiFi 密码，仅 `standalone` 使用，可为空字符串。 |

`mqtt` 字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `host` | string | MQTTS 服务地址，仅 `standalone` 必需。 |
| `port` | number | MQTTS 服务端口，仅 `standalone` 必需，按实际服务配置填写。 |
| `user` | string | MQTT 用户名，仅 `standalone` 使用，可为空字符串。 |
| `password` | string | MQTT 密码，仅 `standalone` 使用，可为空字符串。 |

`espnow` 字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `master_mac` | string | ESP-NOW 主节点 MAC 地址，`espnow_slave` 必需。 |
| `fixed_channel` | number | ESP-NOW 固定信道，可选，合法范围 `1` 到 `13`。 |

## 必填规则

### standalone

`standalone` 是独立上传模式，设备需要自己连接 WiFi 和 MQTT。

必填字段：

- `product_key`
- `device_key`
- `device_role`
- `wifi.ssid`
- `mqtt.host`
- `mqtt.port`

可选字段：

- `device.name`
- `device.version`
- `sensor.type`
- `sensor.pin`
- `sensor.active_level`
- `sensor.debounce_ms`
- `sensor.report_interval_ms`
- `sensor.data_key`
- `sensor.active_value`
- `wifi.password`
- `mqtt.user`
- `mqtt.password`

### espnow_slave

`espnow_slave` 是 ESP-NOW 从节点模式，设备不连接 WiFi/MQTT。

必填字段：

- `product_key`
- `device_key`
- `device_role`
- `espnow.master_mac`

可选字段：

- `device.name`
- `device.version`
- `sensor.type`
- `sensor.pin`
- `sensor.active_level`
- `sensor.debounce_ms`
- `sensor.report_interval_ms`
- `sensor.data_key`
- `sensor.active_value`
- `espnow.fixed_channel`

如果 `espnow.fixed_channel` 未填写，设备启动后从信道 `1` 到 `13` 依次测试，找到可以与 `espnow.master_mac` 通信的信道后使用该信道。

## MQTT Topic 规则

代码内部根据 `product_key` 和 `device_key` 拼接 topic，不由配置传入完整前缀。

数据上报 topic：

```text
/sys/{product_key}/{device_key}/uplink/data
```

状态上报 topic：

```text
/sys/{product_key}/{device_key}/uplink/status
```

示例：

```text
/sys/pk_demo/dk_fridge_001/uplink/data
/sys/pk_demo/dk_fridge_001/uplink/status
```

## Payload 规则

上报 payload 中的 `key` 使用 `device_key`。

门磁数据示例：

```json
{"key":"dk_fridge_001","ts":1710000000,"data":{"door":0}}
```

状态数据示例：

```json
{"key":"dk_fridge_001","ts":1710000000}
```

数据字段说明：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `data.{sensor.data_key}` | number | 门磁业务状态，默认字段名为 `door`，取值为 `0` 或 `1`。 |

默认语义：

| Payload | 说明 |
| --- | --- |
| `{"door":0}` | 关门。默认安装下磁铁靠近 A3144。 |
| `{"door":1}` | 开门。默认安装下磁铁离开 A3144。 |

如果实际安装方向相反，可以把 `sensor.active_value` 配置为 `1`，此时检测到磁铁时上传 `door=1`，未检测到磁铁时上传 `door=0`。

## 串口协议

串口参数：`115200`。

通信格式：JSON Lines，每条命令以换行符结束。

## 串口指令说明

设备通过串口接收 JSON Lines 指令。每条指令必须以换行符结束。

| 指令 | 作用 | 是否修改配置 | 典型使用场景 |
| --- | --- | --- | --- |
| `hello` | 检测设备是否在线，确认串口通信正常。 | 否 | 配置工具扫描串口设备。 |
| `get_config` | 读取当前配置和运行时设备信息。 | 否 | 配置工具展示当前设备配置。 |
| `set_config` | 写入新配置到 NVS。 | 是 | 首次烧录后写入设备身份、角色、传感器、WiFi、MQTT 或 ESP-NOW 参数。 |

### 指令处理规则

1. 如果收到纯文本 `hello`，设备直接返回 `{"status":"ok"}`。
2. 如果收到 JSON 且 `cmd` 为 `hello`，设备直接返回 `{"status":"ok"}`。
3. 如果收到 JSON 解析失败，设备返回 `status=error` 和错误信息。
4. 如果收到未知 `cmd`，设备返回 `status=error` 和 `unknown cmd`。
5. `get_config` 不修改 NVS，只读取当前内存配置并补充运行时设备信息。
6. `set_config` 会校验配置；校验失败不写入 NVS。
7. `set_config` 写入成功后默认重启设备。
8. `set_config` 如果传入 `"restart":false`，设备不立即重启，但会停止当前 WiFi/MQTT 或 ESP-NOW 流程并按新配置运行。

### 配置工具推荐流程

1. 打开串口，参数使用 `115200`。
2. 发送 `hello` 或 `{"cmd":"hello"}`。
3. 收到 `{"status":"ok"}` 后确认设备在线。
4. 发送 `{"cmd":"get_config"}` 读取当前配置。
5. 根据 `configured` 判断设备是否已配置完整。
6. 如果需要写入或修改配置，发送 `set_config`。
7. 如果 `set_config` 使用默认重启，等待设备重启后重新发送 `hello`。
8. 再次发送 `get_config`，确认 `configured=true` 且配置内容正确。
9. 如果角色是 `standalone`，等待设备连接 WiFi 和 MQTT 并开始上报门磁数据。
10. 如果角色是 `espnow_slave`，等待设备进入 ESP-NOW 从节点流程。

### 扫描设备

请求：

```json
{"cmd":"hello"}
```

兼容纯文本：

```text
hello
```

响应：

```json
{"status":"ok"}
```

### 读取配置

请求：

```json
{"cmd":"get_config"}
```

响应示例：

```json
{
  "status": "ok",
  "cmd": "get_config",
  "configured": true,
  "config": {
    "product_key": "pk_demo",
    "device_key": "dk_fridge_001",
    "device_role": "standalone",
    "device": {
      "name": "ESP32-C3 A3144 Door Collector",
      "version": "1.0.0",
      "chip": "ESP32-C3",
      "mac": "A0:B7:65:12:34:56",
      "flash": 4194304
    },
    "sensor": {
      "type": "a3144",
      "pin": 4,
      "active_level": "low",
      "debounce_ms": 50,
      "report_interval_ms": 60000,
      "data_key": "door",
      "active_value": 0
    },
    "wifi": {
      "ssid": "HomeWiFi",
      "password": "password"
    },
    "mqtt": {
      "host": "192.168.1.10",
      "port": 1883,
      "user": "iot",
      "password": "secret"
    },
    "espnow": {
      "master_mac": "",
      "fixed_channel": 6
    }
  }
}
```

### 写入配置

请求命令：`set_config`。

默认写入成功后重启设备。如果不希望立即重启，可以传入：

```json
{"cmd":"set_config","restart":false,"config":{}}
```

写入成功响应示例：

```json
{"status":"ok","cmd":"set_config","configured":true,"restart":true}
```

## standalone 入参示例

`standalone` 设备自己连接 WiFi 和 MQTTS，并将门磁状态直接上传到 MQTT。

```json
{
  "cmd": "set_config",
  "config": {
    "product_key": "pk_demo",
    "device_key": "dk_fridge_001",
    "device_role": "standalone",
    "device": {
      "name": "ESP32-C3 A3144 Door Collector",
      "version": "1.0.0"
    },
    "sensor": {
      "type": "a3144",
      "pin": 4,
      "active_level": "low",
      "debounce_ms": 50,
      "report_interval_ms": 60000,
      "data_key": "door",
      "active_value": 0
    },
    "wifi": {
      "ssid": "HomeWiFi",
      "password": "password"
    },
    "mqtt": {
      "host": "192.168.1.10",
      "port": 1883,
      "user": "iot",
      "password": "secret"
    }
  }
}
```

写入后，该设备会发布到：

```text
/sys/pk_demo/dk_fridge_001/uplink/data
/sys/pk_demo/dk_fridge_001/uplink/status
```

## espnow_slave 入参示例

`espnow_slave` 设备不连接 WiFi/MQTT，只把门磁状态发送给 ESP-NOW 主节点。

### 固定信道

如果已知主节点信道，传入 `espnow.fixed_channel`。

```json
{
  "cmd": "set_config",
  "config": {
    "product_key": "pk_demo",
    "device_key": "dk_fridge_002",
    "device_role": "espnow_slave",
    "device": {
      "name": "ESP32-C3 A3144 Door Collector",
      "version": "1.0.0"
    },
    "sensor": {
      "type": "a3144",
      "pin": 4,
      "active_level": "low",
      "debounce_ms": 50,
      "report_interval_ms": 60000,
      "data_key": "door",
      "active_value": 0
    },
    "espnow": {
      "master_mac": "AA:BB:CC:DD:EE:FF",
      "fixed_channel": 6
    }
  }
}
```

### 自动探测信道

如果不知道主节点信道，不传 `espnow.fixed_channel`。设备会从 `1` 到 `13` 依次测试。

```json
{
  "cmd": "set_config",
  "config": {
    "product_key": "pk_demo",
    "device_key": "dk_fridge_002",
    "device_role": "espnow_slave",
    "device": {
      "name": "ESP32-C3 A3144 Door Collector",
      "version": "1.0.0"
    },
    "sensor": {
      "type": "a3144",
      "pin": 4,
      "active_level": "low",
      "debounce_ms": 50,
      "report_interval_ms": 60000,
      "data_key": "door",
      "active_value": 0
    },
    "espnow": {
      "master_mac": "AA:BB:CC:DD:EE:FF"
    }
  }
}
```

## NVS 存储键

建议 NVS 命名空间使用：`hallcfg`。

建议存储键如下：

| NVS key | 来源字段 |
| --- | --- |
| `product_key` | `product_key` |
| `device_key` | `device_key` |
| `role` | `device_role` |
| `device_name` | `device.name` |
| `device_version` | `device.version` |
| `sensor_type` | `sensor.type` |
| `sensor_pin` | `sensor.pin` |
| `sensor_active_level` | `sensor.active_level` |
| `sensor_debounce_ms` | `sensor.debounce_ms` |
| `sensor_report_ms` | `sensor.report_interval_ms` |
| `sensor_data_key` | `sensor.data_key` |
| `sensor_active_value` | `sensor.active_value` |
| `wifi_ssid` | `wifi.ssid` |
| `wifi_pass` | `wifi.password` |
| `mqtt_host` | `mqtt.host` |
| `mqtt_port` | `mqtt.port` |
| `mqtt_user` | `mqtt.user` |
| `mqtt_pass` | `mqtt.password` |
| `espnow_fixed_ch` | `espnow.fixed_channel` |
| `espnow_master_mac` | `espnow.master_mac` |

## 执行流程

### 通用启动流程

设备每次上电或重启后按以下流程执行：

1. 初始化串口，波特率使用 `115200`。
2. 从 NVS 命名空间 `hallcfg` 读取配置。
3. 校验 `product_key`、`device_key`、`device_role`、`sensor` 和当前角色需要的必填字段。
4. 根据 `sensor.pin` 初始化 A3144 GPIO 输入模式。
5. 读取当前门磁状态，形成初始业务值。
6. 如果配置不完整，设备不连接 WiFi、MQTT 或 ESP-NOW，只保留串口指令处理能力。
7. 如果配置完整，设备根据 `device_role` 进入对应运行流程。
8. 主循环持续处理串口指令，所以设备运行中仍可执行 `hello`、`get_config`、`set_config`。

### 未配置流程

当 `configured=false` 时，设备进入等待配置状态。

1. 设备不连接 WiFi。
2. 设备不连接 MQTT。
3. 设备不初始化 ESP-NOW 业务通信。
4. 设备持续监听串口输入。
5. 收到 `hello` 时返回 `{"status":"ok"}`。
6. 收到 `get_config` 时返回当前空配置和运行时设备信息。
7. 收到合法 `set_config` 后写入 NVS。
8. 如果 `set_config` 未传 `"restart":false`，设备写入成功后重启。
9. 如果 `set_config` 传入 `"restart":false`，设备不重启，直接按新配置重新进入运行判断。

### A3144 采集流程

1. 根据 `sensor.pin` 配置 GPIO 输入。
2. 读取 GPIO 当前电平。
3. 根据 `sensor.active_level` 判断磁铁是否靠近。
4. 如果磁铁靠近，业务值为 `sensor.active_value`。
5. 如果磁铁离开，业务值为 `sensor.active_value` 的相反值。
6. 状态变化必须经过 `sensor.debounce_ms` 去抖确认。
7. 状态变化确认后立即生成只包含业务字段的数据 payload，例如 `{"door":0}`。
8. 如果 `sensor.report_interval_ms > 0`，到达补报间隔后也生成一次当前状态 payload。

### standalone 执行流程

当 `device_role=standalone` 且配置完整时，设备作为独立门磁节点运行。

启动阶段：

1. 读取 `wifi.ssid` 和 `wifi.password`。
2. 调用 WiFi 连接逻辑连接目标路由器。
3. WiFi 连接过程中仍会处理串口指令，避免设备因网络问题无法重新配置。
4. WiFi 连接成功后读取本机 IP、网关、子网掩码和信号强度用于调试输出。
5. 使用 `mqtt.host` 和 `mqtt.port` 设置 MQTTS 服务端。
6. 使用 `device_key` 作为 MQTT client id。
7. 如果配置了 `mqtt.user` 和 `mqtt.password`，连接 MQTT 时带上认证信息。
8. MQTT 连接成功后上报当前门磁状态，并进入状态变化检测流程。

运行循环：

1. 每轮循环先处理串口指令。
2. 轮询 A3144 GPIO，执行去抖和门磁状态判断。
3. 如果门磁业务值发生变化，向 `/sys/{product_key}/{device_key}/uplink/data` 发布数据 payload。
4. 如果 `sensor.report_interval_ms > 0` 且达到补报间隔，向数据 topic 补报当前门磁状态。
5. 如果 WiFi 断开，并且达到重连间隔，则重新连接 WiFi。
6. 如果 WiFi 已连接但 MQTT 未连接，并且达到重连间隔，则重新连接 MQTT。
7. MQTT 已连接时调用 MQTT loop 保持心跳。
8. 到达状态上报间隔后，向 `/sys/{product_key}/{device_key}/uplink/status` 发布状态 payload。
9. 上报 payload 中的 `key` 固定使用 `device_key`。

`standalone` 状态上报 payload：

```json
{"key":"dk_fridge_001","ts":1710000000}
```

`standalone` 数据上报 payload：

```json
{"key":"dk_fridge_001","ts":1710000000,"data":{"door":0}}
```

运行中重新配置：

1. 收到 `set_config` 后先校验新配置。
2. 校验失败时返回 `status=error`，原配置继续生效。
3. 校验成功时写入 NVS，并更新内存中的当前配置。
4. 如果 `restart=true` 或未传 `restart`，设备返回成功响应后重启。
5. 如果 `restart=false`，设备断开当前 MQTT 和 WiFi 连接。
6. 设备按新配置重新初始化 GPIO、WiFi 和 MQTT。

### espnow_slave 执行流程

当 `device_role=espnow_slave` 且配置完整时，设备作为 ESP-NOW 从节点运行。

启动阶段：

1. 读取 `espnow.master_mac` 作为主节点 MAC 地址。
2. 如果配置了 `espnow.fixed_channel`，设备使用该固定信道初始化 ESP-NOW。
3. 如果未配置 `espnow.fixed_channel`，设备从信道 `1` 到 `13` 依次探测主节点。
4. 找到可通信信道后，记录当前运行信道并进入从节点发送流程。
5. `espnow_slave` 不连接业务 WiFi。
6. `espnow_slave` 不连接 MQTT。
7. `espnow_slave` 仍保留串口指令处理能力。

运行循环：

1. 每轮循环先处理串口指令。
2. 轮询 A3144 GPIO，执行去抖和门磁状态判断。
3. 如果 ESP-NOW 未初始化成功，继续执行信道探测或等待重新配置。
4. 如果门磁业务值发生变化，将当前数据封装为从节点数据包。
5. 如果 `sensor.report_interval_ms > 0` 且达到补报间隔，也封装一次当前状态数据包。
6. 通过 ESP-NOW 发送给 `espnow.master_mac` 对应的主节点。
7. 主节点收到后再决定是否转发到 MQTT 或其他上游系统。
8. 从节点 payload 中的设备标识使用 `device_key`。

`espnow_slave` 数据包建议包含：

```json
{"key":"dk_fridge_002","ts":1710000000,"data":{"door":1}}
```

固定信道流程：

1. 配置中存在 `espnow.fixed_channel`。
2. 设备直接切换到该信道。
3. 初始化 ESP-NOW。
4. 添加 `espnow.master_mac` 为 peer。
5. 状态变化时发送门磁数据，并按 `sensor.report_interval_ms` 定时补报。

自动探测信道流程：

1. 配置中不存在 `espnow.fixed_channel` 或值为 `0`。
2. 设备从信道 `1` 开始初始化 ESP-NOW。
3. 设备向主节点发送探测包。
4. 如果收到主节点确认，则使用当前信道。
5. 如果未收到确认，则释放当前 ESP-NOW 状态并尝试下一个信道。
6. 如果信道 `1` 到 `13` 都失败，设备保持等待状态并继续允许串口重新配置。

运行中重新配置：

1. 收到 `set_config` 后先校验新配置。
2. 校验失败时返回 `status=error`，原配置继续生效。
3. 校验成功时写入 NVS，并更新内存中的当前配置。
4. 如果 `restart=true` 或未传 `restart`，设备返回成功响应后重启。
5. 如果 `restart=false`，设备停止当前 ESP-NOW 通信。
6. 设备按新配置重新初始化 GPIO、ESP-NOW 或切换到其他角色流程。

## 校验规则

- `product_key` 不能为空。
- `device_key` 不能为空。
- `device_role` 只能是 `standalone` 或 `espnow_slave`。
- `sensor.type` 只能是 `a3144`。
- `sensor.pin` 必须是合法 GPIO，默认 `4`。
- `sensor.active_level` 只能是 `low` 或 `high`，默认 `low`。
- `sensor.debounce_ms` 必须大于或等于 `0`，默认 `50`。
- `sensor.report_interval_ms` 必须大于或等于 `0`，默认 `60000`；传 `0` 表示关闭定时补报。
- `sensor.data_key` 不能为空，默认 `door`。
- `sensor.active_value` 只能是 `0` 或 `1`，默认 `0`。
- `standalone` 下 `wifi.ssid` 不能为空。
- `standalone` 下 `mqtt.host` 不能为空。
- `standalone` 下 `mqtt.port` 必须大于 `0`。
- `espnow_slave` 下 `espnow.master_mac` 不能为空。
- 如果传入 `espnow.fixed_channel`，必须在 `1` 到 `13` 之间。

## 实现注意事项

- `hello` 只返回 `{"status":"ok"}`。
- `get_config` 返回配置和运行时设备信息。
- `set_config` 入参使用分组结构：`device`、`sensor`、`wifi`、`mqtt`、`espnow`。
- `device.name` 和 `device.version` 是可写入配置。
- `device.chip`、`device.mac`、`device.flash` 是运行时读取值，不应由配置写入。
- `mqtt_topic_prefix` 不应出现在串口配置、NVS 或代码配置结构中。
- `device_id` 应统一替换为 `device_key`。
- MQTT client id 使用 `device_key`。
- MQTT 连接使用 `WiFiClientSecure`，当前与 TH-Collector 一致使用 `setInsecure()`，即启用 TLS 但不校验证书。
- payload 中的 `key` 使用 `device_key`。
- A3144 只作为数字输入处理，不设计模拟量读取。
- 数据 payload 只上报业务值，不上报 GPIO 原始电平、磁铁状态、计数、增量或 RPM。
- 门磁状态变化必须经过 `sensor.debounce_ms` 去抖确认。
- 状态变化时立即上报；`sensor.report_interval_ms > 0` 时定时补报当前状态。
- `sensor.data_key` 默认 `door`，但可配置为 `window`、`drawer` 等其他业务字段名。
- 默认语义是 `door=0` 表示关闭，`door=1` 表示打开。
- 如果实际安装方向相反，通过 `sensor.active_value` 调整，不需要修改代码。
- ESP-NOW 自动探测只在未配置 `espnow.fixed_channel` 时触发。
- `espnow_slave` 模式不应尝试连接业务 WiFi 或 MQTT。
