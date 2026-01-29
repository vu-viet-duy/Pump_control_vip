#include "network.h"
#include "secrets.h"

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

// ===== Command Storage =====
String mqttCmd = "";
String bleCmd = "";

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  String message = "";
  for (unsigned int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }

  Serial.printf("MQTT Received [%s]: %s\n", topic, message.c_str());

  // Handle MQTT commands
  if (strcmp(topic, TOPIC_COMMAND) == 0)
  {
    mqttCmd = message;
    Serial.printf("📨 Command stored: %s\n", mqttCmd.c_str());
  }
}

bool connectWiFi()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    wifiConnected = true;
    return true;
  }

  Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20)
  {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    wifiConnected = true;
    Serial.printf("\n✓ WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }
  else
  {
    wifiConnected = false;
    Serial.println("\n✗ WiFi connection failed!");
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

  Serial.printf("Connecting to MQTT: %s:%d\n", MQTT_BROKER, MQTT_PORT);

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
    Serial.println("✓ MQTT connected!");

    // Subscribe to command topic
    mqttClient.subscribe(TOPIC_COMMAND);
    Serial.printf("Subscribed to: %s\n", TOPIC_COMMAND);

    return true;
  }
  else
  {
    mqttConnected = false;
    Serial.printf("✗ MQTT connection failed! State: %d\n", mqttClient.state());
    return false;
  }
}

void networkInit()
{
  Serial.println("\n=== Network Init ===");

  // Connect WiFi
  connectWiFi();

  // Connect MQTT if WiFi is connected
  if (wifiConnected)
  {
    delay(1000);
    connectMQTT();
  }

  Serial.println("===================\n");
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
      Serial.println("WiFi disconnected, reconnecting...");
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
      Serial.println("MQTT disconnected, reconnecting...");
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
    Serial.println("⚠️  MQTT not connected, skipping publish");
    return;
  }

  char payload[100];

  // Publish water tank
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

  Serial.printf("📡 Published: Water=%.0f%%, Chem=%.0f%%, Mix=%.0f%%\n",
                waterPercent, chemPercent, mixPercent);
}

bool networkIsConnected()
{
  return wifiConnected && mqttConnected;
}
