#ifndef SECRETS_ALT_H
#define SECRETS_ALT_H

// 🔄 ALTERNATIVE WiFi CONFIGS FOR TESTING
// Uncomment the one you want to test

// === CURRENT CONFIG ===
#define WIFI_SSID "Duong Uyen 2"
#define WIFI_PASSWORD "vuduy2000"
#define MQTT_SERVER "broker.emqx.io"
#define MQTT_PORT 8883
#define MQTT_USER ""
#define MQTT_PASS ""

// === MQTT TOPICS ===
#define TOPIC_CMD "pump/cmd"
#define TOPIC_STATUS "pump/status"

// === MQTT CLIENT ID ===
#define MQTT_CLIENT_ID "PumpController_ESP32S3"

#endif