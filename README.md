# ESP32C3-A3144

A3144 霍尔门磁开关采集器，典型场景是冰箱门开关检测。

## 项目说明

1. 使用 ESP32-C3 + A3144 数字霍尔开关检测门磁状态。
2. `standalone` 模式下设备连接 WiFi 和 MQTTS，上报 `door=0/1`。
3. `espnow_slave` 模式下设备通过 ESP-NOW 发送门磁状态给主节点。
4. 设备身份、硬件信息、WiFi、MQTT、ESP-NOW 和 A3144 参数通过串口写入 NVS。

## 默认硬件约定

- A3144 OUT 接 ESP32-C3 `GPIO4`。
- 默认有效电平为 `low`，即检测到磁铁时 GPIO 为低电平。
- 默认 `door=0` 表示关闭，`door=1` 表示打开。
- 默认状态变化去抖时间为 `50ms`。
- 默认每 `60000ms` 定时补报一次当前状态；门状态变化会立即上报。

## 串口配置协议

串口参数：`115200`，通信格式：JSON Lines，每条命令以换行符结束。

### 扫描设备

```json
{"cmd":"hello"}
```

也兼容纯文本：

```text
hello
```

响应：

```json
{"status":"ok"}
```

### 读取配置

```json
{"cmd":"get_config"}
```

### 写入配置

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

默认写入成功后重启。如需不重启，可增加 `"restart":false`。

## 数据上报

数据 topic：

```text
/sys/{product_key}/{device_key}/uplink/data
```

状态 topic：

```text
/sys/{product_key}/{device_key}/uplink/status
```

数据 payload：

```json
{"key":"dk_fridge_001","ts":1710000000,"data":{"door":0}}
```

MQTT 连接使用 `WiFiClientSecure`，当前与 `TH-Collector` 一致使用 `setInsecure()`，即启用 TLS 但不校验证书。

详细设计见 `include/device-config-design.md`。
