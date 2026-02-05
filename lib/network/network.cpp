#include "network.h"
#include "secrets.h"
#include <serial_log.h>

WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);

// ===== MQTT Topics =====
#define TOPIC_STATUS "pump/status"
#define TOPIC_COMMAND "pump/command"
#define TOPIC_WATER "pump/water"
#define TOPIC_CHEM "pump/chem"
#define TOPIC_MIX "pump/mix"

// ===== Connection State =====
static bool wifiConnected = false;
static bool mqttConnected = false;
static unsigned long lastWiFiReconnect = 0;  // Timer riêng cho WiFi
static unsigned long lastMQTTReconnect = 0;  // Timer riêng cho MQTT

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  // Chỉ xử lý topic COMMAND
  if (strcmp(topic, TOPIC_COMMAND) != 0)
    return;

  // Copy payload vào buffer với giới hạn STR_LEN
  char buf[STR_LEN] = {0};
  int copyLen = min((int)length, STR_LEN - 1);
  memcpy(buf, payload, copyLen);
  buf[copyLen] = '\0';

  String cmd(buf);
  cmd.trim();

  if (cmd.length() > 0)
  {
    // ⭐ PUSH TRỰC TIẾP VÀO QUEUE - Thread-safe, không mất lệnh
    cmdLine.println(cmd);
    SerialLog::log("[CMD:MQTT]", cmd);
  }
}

bool connectWiFi()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    wifiConnected = true;
    return true;
  }

  // ⭐ FIX: ESP32-S3 Power Management
  SerialLog::log("🔧 Configuring WiFi power...");
  WiFi.mode(WIFI_STA);
  WiFi.persistent(true);          // Lưu WiFi vào flash
  WiFi.setAutoConnect(true);      // Auto connect khi khởi động
  WiFi.setAutoReconnect(true);    // Auto reconnect khi bị mất
  
  // Disable WiFi sleep mode
  WiFi.setSleep(false);
  
  WiFi.disconnect(true);
  delay(500);

  SerialLog::log("📡 Connecting to:", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) // 15s
  {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    wifiConnected = true;
    SerialLog::log("\n✓ WiFi connected! IP:", WiFi.localIP().toString());
    SerialLog::log("   RSSI:", WiFi.RSSI(), "dBm");
    return true;
  }
  else
  {
    wifiConnected = false;
    SerialLog::log("\n✗ WiFi failed! Status:", WiFi.status());
    return false;
  }
}

bool connectMQTT()
{
  if (mqttClient.connected())
  {
    mqttConnected = true;
    return true;
  }

  if (!wifiConnected)
    return false;

  SerialLog::log("🔗 Connecting to MQTT...");

  // ⭐ FIX: Thử bỏ SSL certificate tạm thời
  // wifiClient.setCACert(ROOT_CA_CERT);
  wifiClient.setInsecure(); // Accept any certificate
  
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setKeepAlive(90);  // Tăng keepalive

  String clientId = MQTT_CLIENT_ID;
  clientId += "_" + String(millis() % 10000); // Random suffix

  if (mqttClient.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD))
  {
    mqttConnected = true;
    SerialLog::log("✓ MQTT connected!");
    mqttClient.subscribe(TOPIC_COMMAND);
    return true;
  }
  else
  {
    mqttConnected = false;
    SerialLog::log("✗ MQTT failed! State:", mqttClient.state());
    return false;
  }
}

void networkInit()
{
  SerialLog::log("\n=== Network Init ===");

  // Connect WiFi
  connectWiFi();

  // Connect MQTT if WiFi is connected
  if (wifiConnected)
  {
    delay(1000);
    connectMQTT();
  }

  SerialLog::log("===================\n");
}

void networkMaintain()
{
  // ⭐ CHECK: WiFi status với ít frequency hơn
  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck > 5000) // Check mỗi 5s thôi
  {
    lastWiFiCheck = millis();
    
    if (WiFi.status() != WL_CONNECTED)
    {
      wifiConnected = false;
      mqttConnected = false;

      // Try WiFi reconnect every 30 seconds
      if (millis() - lastWiFiReconnect > 30000)
      {
        lastWiFiReconnect = millis();
        SerialLog::log("📡 WiFi lost, reconnecting...");
        connectWiFi();
      }
    }
    else
    {
      wifiConnected = true;
    }
  }

  // Maintain MQTT connection (chỉ khi WiFi OK)
  if (wifiConnected && !mqttClient.connected())
  {
    mqttConnected = false;

    // Try MQTT reconnect every 15 seconds
    if (millis() - lastMQTTReconnect > 15000)
    {
      lastMQTTReconnect = millis();
      SerialLog::log("🔗 MQTT lost, reconnecting...");
      connectMQTT();
    }
  }
  else if (mqttClient.connected())
  {
    mqttConnected = true;
  }

  // Process MQTT messages
  if (mqttClient.connected())
  {
    mqttClient.loop();
  }
}

void networkPublish(float waterPercent, float chemPercent, float mixPercent)
{
  if (!mqttClient.connected())
  {
    SerialLog::log("⚠️  MQTT not connected, skipping publish");
    return;
  }

  char payload[100];
  
  snprintf(payload, sizeof(payload), "{\"percent\":%.1f}", waterPercent);
  mqttClient.publish(TOPIC_WATER, payload);

  // Publish chem tank
  snprintf(payload, sizeof(payload), "{\"percent\":%.1f}", chemPercent);
  mqttClient.publish(TOPIC_CHEM, payload);

  // Publish mix tank
  snprintf(payload, sizeof(payload), "{\"percent\":%.1f}", mixPercent);
  mqttClient.publish(TOPIC_MIX, payload);

  // Publish combined status
  snprintf(payload, sizeof(payload),
           "{\"water\":%.1f,\"chem\":%.1f,\"mix\":%.1f}",
           waterPercent, chemPercent, mixPercent);
  mqttClient.publish(TOPIC_STATUS, payload);

  SerialLog::log("📡 Published: Water=", waterPercent, "%, Chem=", chemPercent, "%, Mix=", mixPercent, "%");
}

bool networkIsConnected()
{
  return wifiConnected && mqttConnected;
}