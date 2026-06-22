# 设备配置设计说明

本文档定义 ESP32C3-A3144 固件的设备配置协议。目标是让同一份固件可以烧录到不同 A3144 霍尔传感器采集设备，设备身份、硬件描述、网络参数、运行角色和传感器采集参数通过串口写入 ESP32 NVS 持久化存储。

## 设计目标

- 固件不写死 `PRODUCT_KEY`、`DEVICE_KEY`、WiFi、MQTT 和硬件描述参数。
- 设备首次烧录后通过串口写入配置。
- 配置写入 NVS，设备重启后继续生效。
- MQTT topic 前缀由代码根据 `product_key` 和 `device_key` 自动拼接。
- 不要求外部传入 `mqtt_topic_prefix`。
- `hello` 只用于确认串口通信可用，不返回设备信息。
- 设备配置和硬件信息通过 `get_config` 返回。
- A3144 采集参数集中放在 `sensor` 分组中，避免后续改引脚或有效电平时重新编译固件。

## 传感器模型

A3144 是数字霍尔开关，不输出模拟磁场强度。固件按数字输入处理信号：

- 当前 GPIO 电平用于判断是否检测到磁铁。
- 有效边沿用于累计脉冲数。
- 每个上报周期统计新增脉冲数 `delta`。
- 如果配置了每转触发磁铁数量 `magnets_per_rev`，可按周期新增脉冲数计算 RPM。

默认约定：

| 项 | 默认值 | 说明 |
| --- | --- | --- |
| 信号引脚 | `GPIO4` | 可通过 `sensor.pin` 修改。 |
| 有效电平 | `low` | 常见 A3144 模块检测到磁铁时输出低电平。 |
| 去抖时间 | `10 ms` | 同一有效边沿后短时间内的重复触发忽略。 |
| 上报间隔 | `1000 ms` | 每秒上报一次状态、计数和可选 RPM。 |
| 每转磁铁数 | `1` | 用于 RPM 计算。 |

## 角色定义

`device_role` 用于决定设备运行模式。

| 枚举值 | 说明 |
| --- | --- |
| `standalone` | 独立节点。设备自己连接 WiFi 和 MQTT，采集数据后直接上传到 MQTT。 |
| `espnow_slave` | ESP-NOW 从节点。设备不连接 WiFi/MQTT，只通过 ESP-NOW 将采集数据发送给主节点。 |

## 配置结构

配置根字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `product_key` | string | 产品 Key，用于拼接 MQTT topic。 |
| `device_key` | string | 设备 Key，用于上报 payload 的 `key` 字段，也作为 MQTT client id。 |
| `device_role` | string | 设备角色，可选 `standalone` 或 `espnow_slave`。 |
| `device` | object | 设备信息。`name`、`version` 可写入；`chip`、`mac`、`flash` 由设备运行时读取，只在 `get_config` 返回。 |
| `sensor` | object | A3144 传感器采集参数。 |
| `wifi` | object | WiFi 配置，仅 `standalone` 使用。 |
| `mqtt` | object | MQTT 配置，仅 `standalone` 使用。 |
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
| `pin` | number | `4` | A3144 OUT 信号接入的 ESP32-C3 GPIO。 |
| `active_level` | string | `low` | 检测到磁铁时的有效电平，可选 `low` 或 `high`。 |
| `debounce_ms` | number | `10` | 去抖时间，单位毫秒。 |
| `collect_interval_ms` | number | `1000` | 数据采集和上报间隔，单位毫秒。 |
| `magnets_per_rev` | number | `1` | 每转触发磁铁数，用于计算 RPM。 |

`wifi` 字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `ssid` | string | WiFi SSID，仅 `standalone` 必需。 |
| `password` | string | WiFi 密码，仅 `standalone` 使用，可为空字符串。 |

`mqtt` 字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `host` | string | MQTT 服务地址，仅 `standalone` 必需。 |
| `port` | number | MQTT 服务端口，仅 `standalone` 必需。 |
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
- `sensor.pin`
- `sensor.active_level`
- `sensor.debounce_ms`
- `sensor.collect_interval_ms`
- `sensor.magnets_per_rev`
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
- `sensor.pin`
- `sensor.active_level`
- `sensor.debounce_ms`
- `sensor.collect_interval_ms`
- `sensor.magnets_per_rev`
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
/sys/pk_demo/dk_hall_001/uplink/data
/sys/pk_demo/dk_hall_001/uplink/status
```

## Payload 规则

上报 payload 中的 `key` 使用 `device_key`。

A3144 数据示例：

```json
{"key":"dk_hall_001","ts":1710000000,"data":{"magnet":true,"level":0,"count":128,"delta":3}}
```

状态数据示例：

```json
{"key":"dk_hall_001","ts":1710000000}
```

数据字段说明：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `magnet` | boolean | 当前是否检测到磁铁。由 `level` 和 `sensor.active_level` 计算。 |
| `level` | number | 当前 GPIO 原始电平，`0` 为低电平，`1` 为高电平。 |
| `count` | number | 设备启动以来累计有效脉冲数。 |
| `delta` | number | 当前上报周期内新增有效脉冲数。 |

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
9. 如果角色是 `standalone`，等待设备连接 WiFi 和 MQTT 并开始上报数据。
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
    "device_key": "dk_hall_001",
    "device_role": "standalone",
    "device": {
      "name": "ESP32-C3 A3144 Hall Collector",
      "version": "1.0.0",
      "chip": "ESP32-C3",
      "mac": "A0:B7:65:12:34:56",
      "flash": 4194304
    },
    "sensor": {
      "pin": 2,
      "active_level": "low",
      "debounce_ms": 10,
      "collect_interval_ms": 1000,
      "magnets_per_rev": 1
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

`standalone` 设备自己连接 WiFi 和 MQTT，并将数据直接上传到 MQTT。

```json
{
  "cmd": "set_config",
  "config": {
    "product_key": "pk_demo",
    "device_key": "dk_hall_001",
    "device_role": "standalone",
    "device": {
      "name": "ESP32-C3 A3144 Hall Collector",
      "version": "1.0.0"
    },
    "sensor": {
      "pin": 2,
      "active_level": "low",
      "debounce_ms": 10,
      "collect_interval_ms": 1000,
      "magnets_per_rev": 1
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
/sys/pk_demo/dk_hall_001/uplink/data
/sys/pk_demo/dk_hall_001/uplink/status
```

## espnow_slave 入参示例

`espnow_slave` 设备不连接 WiFi/MQTT，只把采集数据发送给 ESP-NOW 主节点。

### 固定信道

如果已知主节点信道，传入 `espnow.fixed_channel`。

```json
{
  "cmd": "set_config",
  "config": {
    "product_key": "pk_demo",
    "device_key": "dk_hall_002",
    "device_role": "espnow_slave",
    "device": {
      "name": "ESP32-C3 A3144 Hall Collector",
      "version": "1.0.0"
    },
    "sensor": {
      "pin": 2,
      "active_level": "low",
      "debounce_ms": 10,
      "collect_interval_ms": 1000,
      "magnets_per_rev": 1
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
    "device_key": "dk_hall_002",
    "device_role": "espnow_slave",
    "device": {
      "name": "ESP32-C3 A3144 Hall Collector",
      "version": "1.0.0"
    },
    "sensor": {
      "pin": 2,
      "active_level": "low",
      "debounce_ms": 10,
      "collect_interval_ms": 1000,
      "magnets_per_rev": 1
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
| `sensor_pin` | `sensor.pin` |
| `sensor_active_level` | `sensor.active_level` |
| `sensor_debounce_ms` | `sensor.debounce_ms` |
| `sensor_collect_ms` | `sensor.collect_interval_ms` |
| `sensor_magnets_rev` | `sensor.magnets_per_rev` |
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
4. 初始化 A3144 GPIO 输入模式。
5. 如果配置不完整，设备不连接 WiFi、MQTT 或 ESP-NOW，只保留串口指令处理能力。
6. 如果配置完整，设备根据 `device_role` 进入对应运行流程。
7. 主循环持续处理串口指令，所以设备运行中仍可执行 `hello`、`get_config`、`set_config`。

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
2. 读取 GPIO 当前电平，结合 `sensor.active_level` 计算 `magnet`。
3. 记录上一次稳定电平和上一次有效触发时间。
4. 当检测到进入有效电平的边沿，且距离上次有效触发超过 `sensor.debounce_ms`，累计脉冲数 `count` 加一。
5. 每个 `sensor.collect_interval_ms` 周期计算 `delta`。
6. 根据 `delta`、`sensor.collect_interval_ms` 和 `sensor.magnets_per_rev` 计算 `rpm`。
7. 生成数据 payload 并交给当前角色的上报通道。

### standalone 执行流程

当 `device_role=standalone` 且配置完整时，设备作为独立采集节点运行。

启动阶段：

1. 读取 `wifi.ssid` 和 `wifi.password`。
2. 调用 WiFi 连接逻辑连接目标路由器。
3. WiFi 连接过程中仍会处理串口指令，避免设备因网络问题无法重新配置。
4. WiFi 连接成功后读取本机 IP、网关、子网掩码和信号强度用于调试输出。
5. 使用 `mqtt.host` 和 `mqtt.port` 设置 MQTT 服务端。
6. 使用 `device_key` 作为 MQTT client id。
7. 如果配置了 `mqtt.user` 和 `mqtt.password`，连接 MQTT 时带上认证信息。
8. MQTT 连接成功后进入周期采集和上报流程。

运行循环：

1. 每轮循环先处理串口指令。
2. 实时轮询 A3144 GPIO，执行去抖和脉冲计数。
3. 如果 WiFi 断开，并且达到重连间隔，则重新连接 WiFi。
4. 如果 WiFi 已连接但 MQTT 未连接，并且达到重连间隔，则重新连接 MQTT。
5. MQTT 已连接时调用 MQTT loop 保持心跳。
6. 到达状态上报间隔后，向 `/sys/{product_key}/{device_key}/uplink/status` 发布状态 payload。
7. 到达采集间隔后，向 `/sys/{product_key}/{device_key}/uplink/data` 发布 A3144 数据 payload。
8. 如果 WiFi 或 MQTT 未连接，仍继续本地计数；恢复连接后的下一次数据上报使用当前累计值和当前周期 `delta`。
9. 上报 payload 中的 `key` 固定使用 `device_key`。

`standalone` 状态上报 payload：

```json
{"key":"dk_hall_001","ts":1710000000}
```

`standalone` 数据上报 payload：

```json
{"key":"dk_hall_001","ts":1710000000,"data":{"magnet":true,"level":0,"count":128,"delta":3,"rpm":180.0}}
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
2. 实时轮询 A3144 GPIO，执行去抖和脉冲计数。
3. 如果 ESP-NOW 未初始化成功，继续执行信道探测或等待重新配置。
4. 到达采集间隔后，将当前 A3144 数据封装为从节点数据包。
5. 通过 ESP-NOW 发送给 `espnow.master_mac` 对应的主节点。
6. 主节点收到后再决定是否转发到 MQTT 或其他上游系统。
7. 从节点 payload 中的设备标识使用 `device_key`。

`espnow_slave` 数据包建议包含：

```json
{"key":"dk_hall_002","ts":1710000000,"data":{"magnet":false,"level":1,"count":42,"delta":0,"rpm":0.0}}
```

固定信道流程：

1. 配置中存在 `espnow.fixed_channel`。
2. 设备直接切换到该信道。
3. 初始化 ESP-NOW。
4. 添加 `espnow.master_mac` 为 peer。
5. 周期采集并发送数据。

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
- `sensor.pin` 必须是合法 GPIO，默认 `2`。
- `sensor.active_level` 只能是 `low` 或 `high`，默认 `low`。
- `sensor.debounce_ms` 必须大于或等于 `0`，建议不小于 `5`。
- `sensor.collect_interval_ms` 必须大于 `0`，建议不小于 `100`。
- `sensor.magnets_per_rev` 必须大于或等于 `0`，默认 `1`。
- `standalone` 下 `wifi.ssid` 不能为空。
- `standalone` 下 `mqtt.host` 不能为空。
- `standalone` 下 `mqtt.port` 必须大于 `0`。
- `espnow_slave` 下 `espnow.master_mac` 不能为空。
- 如果传入 `espnow.fixed_channel`，必须在 `1` 到 `13` 之间。

## 实现注意事项

- 本文档只定义协议和设计约束，当前不要求编写固件代码。
- `hello` 只返回 `{"status":"ok"}`。
- `get_config` 返回配置和运行时设备信息。
- `set_config` 入参使用分组结构：`device`、`sensor`、`wifi`、`mqtt`、`espnow`。
- `device.name` 和 `device.version` 是可写入配置。
- `device.chip`、`device.mac`、`device.flash` 是运行时读取值，不应由配置写入。
- `mqtt_topic_prefix` 不应出现在串口配置、NVS 或代码配置结构中。
- `device_id` 应统一替换为 `device_key`。
- MQTT client id 使用 `device_key`。
- payload 中的 `key` 使用 `device_key`。
- A3144 只作为数字输入处理，不设计模拟量读取。
- 脉冲计数应只在进入有效电平的边沿增加，不能在有效电平保持期间重复增加。
- 去抖逻辑应先于计数逻辑，避免抖动导致 `count` 和 `delta` 偏大。
- 即使 MQTT 或 ESP-NOW 暂时不可用，本地脉冲计数也应继续运行。
- ESP-NOW 自动探测只在未配置 `espnow.fixed_channel` 时触发。
- `espnow_slave` 模式不应尝试连接业务 WiFi 或 MQTT。
