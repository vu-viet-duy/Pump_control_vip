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
static unsigned long lastReconnectAttempt = 0;

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

  SerialLog::log("Connecting to WiFi:", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20)
  {
    delay(500);
    SerialLog::log(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    wifiConnected = true;
    SerialLog::log("\n✓ WiFi connected! IP:", WiFi.localIP().toString());
    return true;
  }
  else
  {
    wifiConnected = false;
    SerialLog::log("\n✗ WiFi connection failed!");
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

  SerialLog::log("Connecting to MQTT:", MQTT_BROKER, ":", MQTT_PORT);

  // Configure MQTT
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  // Set SSL certificate for secure connection
  wifiClient.setCACert(ROOT_CA_CERT);

  // Connect to MQTT
  String clientId = MQTT_CLIENT_ID;

  if (mqttClient.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD))
  {
    mqttConnected = true;
    SerialLog::log("✓ MQTT connected!");

    // Subscribe to command topic
    mqttClient.subscribe(TOPIC_COMMAND);
    SerialLog::log("Subscribed to:", TOPIC_COMMAND);

    return true;
  }
  else
  {
    mqttConnected = false;
    SerialLog::log("✗ MQTT connection failed! State:", mqttClient.state());
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
  // Maintain WiFi connection
  if (WiFi.status() != WL_CONNECTED)
  {
    wifiConnected = false;
    mqttConnected = false;

    // Try reconnect every 30 seconds
    if (millis() - lastReconnectAttempt > 30000)
    {
      lastReconnectAttempt = millis();
      SerialLog::log("WiFi disconnected, reconnecting...");
      connectWiFi();
    }
  }
  else
  {
    wifiConnected = true;
  }

  // Maintain MQTT connection
  if (wifiConnected && !mqttClient.connected())
  {
    mqttConnected = false;

    // Try reconnect every 10 seconds
    if (millis() - lastReconnectAttempt > 10000)
    {
      lastReconnectAttempt = millis();
      SerialLog::log("MQTT disconnected, reconnecting...");
      connectMQTT();
    }
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
