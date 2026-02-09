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
  if (strcmp(topic, TOPIC_COMMAND) != 0) return;

  char buf[STR_LEN] = {0};
  int copyLen = min((int)length, STR_LEN - 1);
  memcpy(buf, payload, copyLen);
  buf[copyLen] = '\0';

  String cmd(buf);
  cmd.trim();

  if (cmd.length() > 0) {
    cmdLine.println(cmd);
    SerialLog::log("[MQTT]", cmd);
  }
}

bool connectWiFi()
{
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    return true;
  }

  SerialLog::log("🔌 Connecting WiFi:", WIFI_SSID);
  
  // Simple, reliable config
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true); // Let ESP32 handle reconnects
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // Wait up to 15 seconds
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    SerialLog::log("✓ WiFi OK | IP:", WiFi.localIP().toString(), "| RSSI:", WiFi.RSSI());
    return true;
  }

  wifiConnected = false;
  SerialLog::log("❌ WiFi failed");
  return false;
}

bool connectMQTT()
{
  if (mqttClient.connected()) {
    mqttConnected = true;
    return true;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  SerialLog::log("🔗 Connecting MQTT:", MQTT_BROKER);

  wifiClient.setInsecure();
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setKeepAlive(60);

  String clientId = String(MQTT_CLIENT_ID) + "_" + String(random(10000));

  bool connected = (strlen(MQTT_USERNAME) > 0) 
    ? mqttClient.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD)
    : mqttClient.connect(clientId.c_str());

  if (connected) {
    mqttConnected = true;
    mqttClient.subscribe(TOPIC_COMMAND);
    SerialLog::log("✓ MQTT OK | ID:", clientId);
    return true;
  }

  mqttConnected = false;
  SerialLog::log("❌ MQTT failed | Code:", mqttClient.state());
  return false;
}

void networkInit()
{
  SerialLog::log("=== Network Init ===");
  
  if (connectWiFi()) {
    delay(1000);
    connectMQTT();
  }
  
  SerialLog::log("===================");
}

void networkMaintain()
{
  unsigned long now = millis();

  // Throttle reconnect attempts - only every 30 seconds
  if (now - lastReconnectAttempt < 30000) {
    // Just process MQTT messages if connected
    if (mqttClient.connected()) {
      mqttClient.loop();
    }
    return;
  }

  lastReconnectAttempt = now;

  // Check and reconnect WiFi
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    mqttConnected = false;
    SerialLog::log("📡 WiFi lost, reconnecting...");
    
    if (connectWiFi()) {
      delay(1000);
      connectMQTT();
    }
  } 
  // WiFi OK, check MQTT
  else {
    wifiConnected = true;
    
    if (!mqttClient.connected()) {
      mqttConnected = false;
      SerialLog::log("🔗 MQTT lost, reconnecting...");
      connectMQTT();
    } else {
      mqttConnected = true;
      mqttClient.loop();
    }
  }
}

void networkPublish(float waterVolume, float chemVolume, float mixVolume)
{
  if (!mqttClient.connected()) {
    return;
  }

  char payload[100];

  // Publish individual tanks (volume in Liters)
  snprintf(payload, sizeof(payload), "volume :%.1f", waterVolume);
  mqttClient.publish(TOPIC_WATER, payload, false);

  snprintf(payload, sizeof(payload), "volume:%.1f", chemVolume);
  mqttClient.publish(TOPIC_CHEM, payload, false);

  snprintf(payload, sizeof(payload), "volume :%.1f", mixVolume);
  mqttClient.publish(TOPIC_MIX, payload, false);

  // Publish combined status
  snprintf(payload, sizeof(payload),
           "wate :%.1f, che :%.1f, mix:%.1f",
           waterVolume, chemVolume, mixVolume);
  mqttClient.publish(TOPIC_STATUS, payload, false);

  SerialLog::log("📡 Published | W:", waterVolume, "L C:", chemVolume, "L M:", mixVolume, "L");
}

bool networkIsConnected()
{
  return wifiConnected && mqttConnected;
}