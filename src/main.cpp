#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <time.h>

const unsigned long WIFI_RECONNECT_INTERVAL = 5000;
const unsigned long MQTT_RECONNECT_INTERVAL = 10000;
const unsigned long STATUS_PUBLISH_INTERVAL = 30000;
const unsigned long ESPNOW_RETRY_INTERVAL = 2000;

struct DeviceConfig
{
  String productKey;
  String deviceKey;
  String deviceRole;
  String deviceName;
  String deviceVersion;
  String sensorType;
  int sensorPin;
  String sensorActiveLevel;
  unsigned long sensorDebounceMs;
  unsigned long sensorReportIntervalMs;
  String sensorDataKey;
  int sensorActiveValue;
  String wifiSsid;
  String wifiPassword;
  String mqttHost;
  int mqttPort;
  String mqttUser;
  String mqttPassword;
  String espnowMasterMac;
  int espnowFixedChannel;
};

Preferences preferences;
WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);
DeviceConfig config;
String serialLine;

int stableDoorValue = -1;
int candidateDoorValue = -1;
unsigned long candidateChangedAt = 0;
unsigned long lastDoorReportAt = 0;
bool pendingDoorPublish = false;

unsigned long lastWiFiAttemptAt = 0;
unsigned long lastMQTTAttemptAt = 0;
unsigned long lastStatusPublishAt = 0;
unsigned long lastEspNowAttemptAt = 0;

bool espNowInitialized = false;
bool espNowSendFailed = false;
int espNowChannel = 0;
uint8_t espNowMaster[6] = {0};

DeviceConfig defaultConfig()
{
  DeviceConfig c;
  c.productKey = "";
  c.deviceKey = "";
  c.deviceRole = "standalone";
  c.deviceName = "";
  c.deviceVersion = "";
  c.sensorType = "a3144";
  c.sensorPin = 4;
  c.sensorActiveLevel = "low";
  c.sensorDebounceMs = 50;
  c.sensorReportIntervalMs = 60000;
  c.sensorDataKey = "door";
  c.sensorActiveValue = 0;
  c.wifiSsid = "";
  c.wifiPassword = "";
  c.mqttHost = "";
  c.mqttPort = 1883;
  c.mqttUser = "";
  c.mqttPassword = "";
  c.espnowMasterMac = "";
  c.espnowFixedChannel = 0;
  return c;
}

bool isStandaloneRole(const DeviceConfig &c)
{
  return c.deviceRole == "standalone";
}

bool isEspNowSlaveRole(const DeviceConfig &c)
{
  return c.deviceRole == "espnow_slave";
}

bool isPlainJsonKey(const String &key)
{
  if (key.length() == 0 || key.length() > 32)
  {
    return false;
  }

  for (size_t i = 0; i < key.length(); i++)
  {
    char c = key.charAt(i);
    bool ok = (c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_';
    if (!ok)
    {
      return false;
    }
  }

  return true;
}

bool parseMacAddress(const String &text, uint8_t mac[6])
{
  int values[6];
  if (sscanf(text.c_str(), "%x:%x:%x:%x:%x:%x",
             &values[0], &values[1], &values[2],
             &values[3], &values[4], &values[5]) != 6)
  {
    return false;
  }

  for (int i = 0; i < 6; i++)
  {
    if (values[i] < 0 || values[i] > 255)
    {
      return false;
    }
    mac[i] = static_cast<uint8_t>(values[i]);
  }

  return true;
}

bool isConfigReady(const DeviceConfig &c)
{
  if (c.productKey.length() == 0 ||
      c.deviceKey.length() == 0 ||
      c.deviceRole.length() == 0)
  {
    return false;
  }

  if (c.sensorType != "a3144" ||
      c.sensorPin < 0 ||
      c.sensorPin > 21 ||
      (c.sensorActiveLevel != "low" && c.sensorActiveLevel != "high") ||
      !isPlainJsonKey(c.sensorDataKey) ||
      (c.sensorActiveValue != 0 && c.sensorActiveValue != 1))
  {
    return false;
  }

  if (c.espnowFixedChannel < 0 || c.espnowFixedChannel > 13)
  {
    return false;
  }

  if (isStandaloneRole(c))
  {
    return c.wifiSsid.length() > 0 &&
           c.mqttHost.length() > 0 &&
           c.mqttPort > 0;
  }

  if (isEspNowSlaveRole(c))
  {
    uint8_t mac[6];
    return parseMacAddress(c.espnowMasterMac, mac);
  }

  return false;
}

bool isConfigReady()
{
  return isConfigReady(config);
}

int safeSensorPin()
{
  return (config.sensorPin >= 0 && config.sensorPin <= 21) ? config.sensorPin : 4;
}

void loadConfig()
{
  DeviceConfig defaults = defaultConfig();
  preferences.begin("hallcfg", true);
  config.productKey = preferences.getString("product_key", defaults.productKey);
  config.deviceKey = preferences.getString("device_key", defaults.deviceKey);
  config.deviceRole = preferences.getString("role", defaults.deviceRole);
  config.deviceName = preferences.getString("device_name", defaults.deviceName);
  config.deviceVersion = preferences.getString("device_version", defaults.deviceVersion);
  config.sensorType = preferences.getString("sensor_type", defaults.sensorType);
  config.sensorPin = preferences.getInt("sensor_pin", defaults.sensorPin);
  config.sensorActiveLevel = preferences.getString("sensor_active_level", defaults.sensorActiveLevel);
  config.sensorDebounceMs = preferences.getULong("sensor_debounce_ms", defaults.sensorDebounceMs);
  config.sensorReportIntervalMs = preferences.getULong("sensor_report_ms", defaults.sensorReportIntervalMs);
  config.sensorDataKey = preferences.getString("sensor_data_key", defaults.sensorDataKey);
  config.sensorActiveValue = preferences.getInt("sensor_active_value", defaults.sensorActiveValue);
  config.wifiSsid = preferences.getString("wifi_ssid", defaults.wifiSsid);
  config.wifiPassword = preferences.getString("wifi_pass", defaults.wifiPassword);
  config.mqttHost = preferences.getString("mqtt_host", defaults.mqttHost);
  config.mqttPort = preferences.getInt("mqtt_port", defaults.mqttPort);
  config.mqttUser = preferences.getString("mqtt_user", defaults.mqttUser);
  config.mqttPassword = preferences.getString("mqtt_pass", defaults.mqttPassword);
  config.espnowMasterMac = preferences.getString("espnow_master_mac", defaults.espnowMasterMac);
  config.espnowFixedChannel = preferences.getInt("espnow_fixed_ch", defaults.espnowFixedChannel);
  preferences.end();
}

void saveConfig(const DeviceConfig &c)
{
  preferences.begin("hallcfg", false);
  preferences.putString("product_key", c.productKey);
  preferences.putString("device_key", c.deviceKey);
  preferences.putString("role", c.deviceRole);
  preferences.putString("device_name", c.deviceName);
  preferences.putString("device_version", c.deviceVersion);
  preferences.putString("sensor_type", c.sensorType);
  preferences.putInt("sensor_pin", c.sensorPin);
  preferences.putString("sensor_active_level", c.sensorActiveLevel);
  preferences.putULong("sensor_debounce_ms", c.sensorDebounceMs);
  preferences.putULong("sensor_report_ms", c.sensorReportIntervalMs);
  preferences.putString("sensor_data_key", c.sensorDataKey);
  preferences.putInt("sensor_active_value", c.sensorActiveValue);
  preferences.putString("wifi_ssid", c.wifiSsid);
  preferences.putString("wifi_pass", c.wifiPassword);
  preferences.putString("mqtt_host", c.mqttHost);
  preferences.putInt("mqtt_port", c.mqttPort);
  preferences.putString("mqtt_user", c.mqttUser);
  preferences.putString("mqtt_pass", c.mqttPassword);
  preferences.putString("espnow_master_mac", c.espnowMasterMac);
  preferences.putInt("espnow_fixed_ch", c.espnowFixedChannel);
  preferences.end();
}

String topicPath(const char *suffix)
{
  String topic = "/sys/";
  topic += config.productKey;
  topic += "/";
  topic += config.deviceKey;
  topic += "/";
  topic += suffix;
  return topic;
}

time_t currentTimestamp()
{
  time_t now = time(nullptr);
  return now > 1700000000 ? now : 0;
}

void writeConfigJson(JsonObject target, const DeviceConfig &c)
{
  target["product_key"] = c.productKey;
  target["device_key"] = c.deviceKey;
  target["device_role"] = c.deviceRole;

  JsonObject device = target["device"].to<JsonObject>();
  device["name"] = c.deviceName;
  device["version"] = c.deviceVersion;
  device["chip"] = ESP.getChipModel();
  device["mac"] = WiFi.macAddress();
  device["flash"] = ESP.getFlashChipSize();

  JsonObject sensor = target["sensor"].to<JsonObject>();
  sensor["type"] = c.sensorType;
  sensor["pin"] = c.sensorPin;
  sensor["active_level"] = c.sensorActiveLevel;
  sensor["debounce_ms"] = c.sensorDebounceMs;
  sensor["report_interval_ms"] = c.sensorReportIntervalMs;
  sensor["data_key"] = c.sensorDataKey;
  sensor["active_value"] = c.sensorActiveValue;

  JsonObject wifi = target["wifi"].to<JsonObject>();
  wifi["ssid"] = c.wifiSsid;
  wifi["password"] = c.wifiPassword;

  JsonObject mqtt = target["mqtt"].to<JsonObject>();
  mqtt["host"] = c.mqttHost;
  mqtt["port"] = c.mqttPort;
  mqtt["user"] = c.mqttUser;
  mqtt["password"] = c.mqttPassword;

  JsonObject espnow = target["espnow"].to<JsonObject>();
  espnow["master_mac"] = c.espnowMasterMac;
  espnow["fixed_channel"] = c.espnowFixedChannel;
}

void sendJson(JsonDocument &doc)
{
  serializeJson(doc, Serial);
  Serial.println();
}

void sendError(const char *cmd, const char *message)
{
  JsonDocument doc;
  doc["status"] = "error";
  doc["cmd"] = cmd;
  doc["error"] = message;
  sendJson(doc);
}

void sendHello()
{
  JsonDocument doc;
  doc["status"] = "ok";
  sendJson(doc);
}

void sendCurrentConfig(const char *cmd)
{
  JsonDocument doc;
  doc["status"] = "ok";
  doc["cmd"] = cmd;
  doc["configured"] = isConfigReady();
  JsonObject cfg = doc["config"].to<JsonObject>();
  writeConfigJson(cfg, config);
  sendJson(doc);
}

bool applyConfigJson(JsonObjectConst source, DeviceConfig &target)
{
  if (source["product_key"].is<const char *>())
    target.productKey = source["product_key"].as<String>();
  if (source["device_key"].is<const char *>())
    target.deviceKey = source["device_key"].as<String>();
  if (source["device_role"].is<const char *>())
    target.deviceRole = source["device_role"].as<String>();

  JsonObjectConst device = source["device"].as<JsonObjectConst>();
  if (device["name"].is<const char *>())
    target.deviceName = device["name"].as<String>();
  if (device["version"].is<const char *>())
    target.deviceVersion = device["version"].as<String>();

  JsonObjectConst sensor = source["sensor"].as<JsonObjectConst>();
  if (sensor["type"].is<const char *>())
    target.sensorType = sensor["type"].as<String>();
  if (sensor["pin"].is<int>())
    target.sensorPin = sensor["pin"].as<int>();
  if (sensor["active_level"].is<const char *>())
    target.sensorActiveLevel = sensor["active_level"].as<String>();
  if (!sensor["debounce_ms"].isNull())
    target.sensorDebounceMs = sensor["debounce_ms"].as<unsigned long>();
  if (!sensor["report_interval_ms"].isNull())
    target.sensorReportIntervalMs = sensor["report_interval_ms"].as<unsigned long>();
  if (sensor["data_key"].is<const char *>())
    target.sensorDataKey = sensor["data_key"].as<String>();
  if (sensor["active_value"].is<int>())
    target.sensorActiveValue = sensor["active_value"].as<int>();

  JsonObjectConst wifi = source["wifi"].as<JsonObjectConst>();
  if (wifi["ssid"].is<const char *>())
    target.wifiSsid = wifi["ssid"].as<String>();
  if (wifi["password"].is<const char *>())
    target.wifiPassword = wifi["password"].as<String>();

  JsonObjectConst mqtt = source["mqtt"].as<JsonObjectConst>();
  if (mqtt["host"].is<const char *>())
    target.mqttHost = mqtt["host"].as<String>();
  if (mqtt["port"].is<int>())
    target.mqttPort = mqtt["port"].as<int>();
  if (mqtt["user"].is<const char *>())
    target.mqttUser = mqtt["user"].as<String>();
  if (mqtt["password"].is<const char *>())
    target.mqttPassword = mqtt["password"].as<String>();

  JsonObjectConst espnow = source["espnow"].as<JsonObjectConst>();
  if (espnow["master_mac"].is<const char *>())
    target.espnowMasterMac = espnow["master_mac"].as<String>();
  if (espnow["fixed_channel"].is<int>())
    target.espnowFixedChannel = espnow["fixed_channel"].as<int>();

  return isConfigReady(target);
}

int rawDoorValue()
{
  int level = digitalRead(safeSensorPin()) == HIGH ? 1 : 0;
  int activeLevel = config.sensorActiveLevel == "low" ? 0 : 1;
  bool magnetDetected = level == activeLevel;
  return magnetDetected ? config.sensorActiveValue : 1 - config.sensorActiveValue;
}

void initializeDoorInput()
{
  pinMode(safeSensorPin(), INPUT_PULLUP);
  delay(10);
  int value = rawDoorValue();
  stableDoorValue = value;
  candidateDoorValue = value;
  candidateChangedAt = millis();
  pendingDoorPublish = true;
  lastDoorReportAt = 0;
}

void pollDoorState(unsigned long now)
{
  int current = rawDoorValue();
  if (current != candidateDoorValue)
  {
    candidateDoorValue = current;
    candidateChangedAt = now;
  }

  if (candidateDoorValue != stableDoorValue &&
      now - candidateChangedAt >= config.sensorDebounceMs)
  {
    stableDoorValue = candidateDoorValue;
    pendingDoorPublish = true;
  }
}

void buildDoorPayload(char *buffer, size_t len)
{
  snprintf(buffer, len,
           "{\"key\":\"%s\",\"ts\":%ld,\"data\":{\"%s\":%d}}",
           config.deviceKey.c_str(),
           static_cast<long>(currentTimestamp()),
           config.sensorDataKey.c_str(),
           stableDoorValue < 0 ? rawDoorValue() : stableDoorValue);
}

void buildStatusPayload(char *buffer, size_t len)
{
  snprintf(buffer, len,
           "{\"key\":\"%s\",\"ts\":%ld}",
           config.deviceKey.c_str(),
           static_cast<long>(currentTimestamp()));
}

bool shouldReportDoor(unsigned long now)
{
  if (pendingDoorPublish)
  {
    return true;
  }

  return config.sensorReportIntervalMs > 0 &&
         (lastDoorReportAt == 0 || now - lastDoorReportAt >= config.sensorReportIntervalMs);
}

void stopEspNow()
{
  if (espNowInitialized)
  {
    esp_now_deinit();
    espNowInitialized = false;
  }
}

void onEspNowSent(const uint8_t *, esp_now_send_status_t status)
{
  espNowSendFailed = status != ESP_NOW_SEND_SUCCESS;
}

bool initializeEspNow(int channel)
{
  uint8_t mac[6];
  if (!parseMacAddress(config.espnowMasterMac, mac))
  {
    return false;
  }
  memcpy(espNowMaster, mac, sizeof(espNowMaster));

  stopEspNow();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  int selectedChannel = channel > 0 ? channel : 1;
  esp_wifi_set_channel(selectedChannel, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK)
  {
    return false;
  }

  esp_now_register_send_cb(onEspNowSent);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, espNowMaster, sizeof(espNowMaster));
  peer.channel = selectedChannel;
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK)
  {
    esp_now_deinit();
    return false;
  }

  espNowChannel = selectedChannel;
  espNowInitialized = true;
  espNowSendFailed = false;
  Serial.printf("ESP-NOW initialized, channel: %d\n", espNowChannel);
  return true;
}

void retryEspNowChannel(unsigned long now)
{
  if (!isEspNowSlaveRole(config) || config.espnowFixedChannel > 0)
  {
    return;
  }

  if (!espNowSendFailed || now - lastEspNowAttemptAt < ESPNOW_RETRY_INTERVAL)
  {
    return;
  }

  lastEspNowAttemptAt = now;
  int nextChannel = espNowChannel >= 13 ? 1 : espNowChannel + 1;
  initializeEspNow(nextChannel);
  pendingDoorPublish = true;
}

bool publishDoorToEspNow(unsigned long now)
{
  retryEspNowChannel(now);
  if (!espNowInitialized)
  {
    int channel = config.espnowFixedChannel > 0 ? config.espnowFixedChannel : 1;
    if (!initializeEspNow(channel))
    {
      return false;
    }
  }

  char payload[160];
  buildDoorPayload(payload, sizeof(payload));
  esp_err_t result = esp_now_send(espNowMaster, reinterpret_cast<const uint8_t *>(payload), strlen(payload) + 1);
  if (result == ESP_OK)
  {
    Serial.println(payload);
    pendingDoorPublish = false;
    lastDoorReportAt = now;
    return true;
  }

  espNowSendFailed = true;
  return false;
}

bool isWiFiConnected()
{
  return WiFi.status() == WL_CONNECTED;
}

bool isMqttConnected()
{
  return mqttClient.connected();
}

bool connectWiFi(unsigned long now)
{
  if (lastWiFiAttemptAt != 0 && now - lastWiFiAttemptAt < WIFI_RECONNECT_INTERVAL)
  {
    return false;
  }

  lastWiFiAttemptAt = now;
  Serial.printf("connect WiFi, SSID: %s\n", config.wifiSsid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.wifiSsid.c_str(), config.wifiPassword.c_str());
  return true;
}

bool connectMqtt(unsigned long now)
{
  if (lastMQTTAttemptAt != 0 && now - lastMQTTAttemptAt < MQTT_RECONNECT_INTERVAL)
  {
    return false;
  }

  lastMQTTAttemptAt = now;
  mqttClient.setServer(config.mqttHost.c_str(), config.mqttPort);
  const char *mqttUser = config.mqttUser.length() > 0 ? config.mqttUser.c_str() : nullptr;
  const char *mqttPassword = config.mqttPassword.length() > 0 ? config.mqttPassword.c_str() : nullptr;
  Serial.printf("connect MQTT, host: %s:%d, client id: %s\n",
                config.mqttHost.c_str(), config.mqttPort, config.deviceKey.c_str());

  if (mqttClient.connect(config.deviceKey.c_str(), mqttUser, mqttPassword))
  {
    pendingDoorPublish = true;
    return true;
  }

  Serial.printf("connect MQTT failed, state: %d\n", mqttClient.state());
  return false;
}

bool publishDoorToMqtt(unsigned long now)
{
  char payload[160];
  buildDoorPayload(payload, sizeof(payload));
  String topic = topicPath("uplink/data");
  if (mqttClient.publish(topic.c_str(), payload))
  {
    Serial.println(payload);
    pendingDoorPublish = false;
    lastDoorReportAt = now;
    return true;
  }

  return false;
}

void publishStatusToMqtt(unsigned long now)
{
  if (now - lastStatusPublishAt < STATUS_PUBLISH_INTERVAL)
  {
    return;
  }

  lastStatusPublishAt = now;
  char payload[96];
  buildStatusPayload(payload, sizeof(payload));
  String topic = topicPath("uplink/status");
  mqttClient.publish(topic.c_str(), payload);
  Serial.println(payload);
}

void resetRuntimeAfterConfigChange()
{
  mqttClient.disconnect();
  WiFi.disconnect();
  stopEspNow();
  initializeDoorInput();
  lastWiFiAttemptAt = 0;
  lastMQTTAttemptAt = 0;
  lastStatusPublishAt = 0;
  lastEspNowAttemptAt = 0;
}

void handleSerialCommand(const String &line)
{
  if (line == "hello")
  {
    sendHello();
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err)
  {
    sendError("unknown", "invalid json");
    return;
  }

  const char *cmd = doc["cmd"] | "";
  if (strcmp(cmd, "hello") == 0)
  {
    sendHello();
    return;
  }

  if (strcmp(cmd, "get_config") == 0)
  {
    sendCurrentConfig(cmd);
    return;
  }

  if (strcmp(cmd, "set_config") == 0)
  {
    JsonObjectConst cfg = doc["config"].as<JsonObjectConst>();
    if (cfg.isNull())
    {
      sendError(cmd, "missing config");
      return;
    }

    DeviceConfig next = config;
    if (!applyConfigJson(cfg, next))
    {
      sendError(cmd, "invalid config");
      return;
    }

    saveConfig(next);
    config = next;
    bool shouldRestart = doc["restart"] | true;

    JsonDocument res;
    res["status"] = "ok";
    res["cmd"] = cmd;
    res["configured"] = isConfigReady();
    res["restart"] = shouldRestart;
    sendJson(res);

    if (shouldRestart)
    {
      delay(300);
      ESP.restart();
    }

    resetRuntimeAfterConfigChange();
    return;
  }

  sendError(cmd, "unknown cmd");
}

void processSerialCommands()
{
  while (Serial.available() > 0)
  {
    char ch = Serial.read();
    if (ch == '\r')
    {
      continue;
    }
    if (ch == '\n')
    {
      serialLine.trim();
      if (serialLine.length() > 0)
      {
        handleSerialCommand(serialLine);
      }
      serialLine = "";
      continue;
    }

    if (serialLine.length() < 1024)
    {
      serialLine += ch;
    }
  }
}

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32-C3 A3144 door collector");

  loadConfig();
  initializeDoorInput();
  Serial.printf("config status: %s\n", isConfigReady() ? "configured" : "pending");

  WiFi.mode(WIFI_STA);
  wifiClient.setInsecure();
  mqttClient.setSocketTimeout(3);
  configTime(8 * 3600, 0, "ntp.aliyun.com", "ntp1.aliyun.com");

  if (isConfigReady() && isEspNowSlaveRole(config))
  {
    int channel = config.espnowFixedChannel > 0 ? config.espnowFixedChannel : 1;
    initializeEspNow(channel);
  }
}

void loop()
{
  processSerialCommands();
  unsigned long now = millis();

  if (!isConfigReady())
  {
    delay(100);
    return;
  }

  pollDoorState(now);

  if (isStandaloneRole(config))
  {
    stopEspNow();

    if (!isWiFiConnected())
    {
      connectWiFi(now);
      delay(100);
      return;
    }

    if (!isMqttConnected())
    {
      connectMqtt(now);
      delay(100);
      return;
    }

    mqttClient.loop();
    if (shouldReportDoor(now))
    {
      publishDoorToMqtt(now);
    }
    publishStatusToMqtt(now);
    delay(20);
    return;
  }

  if (isEspNowSlaveRole(config))
  {
    if (shouldReportDoor(now))
    {
      publishDoorToEspNow(now);
    }
    delay(20);
    return;
  }

  delay(100);
}
