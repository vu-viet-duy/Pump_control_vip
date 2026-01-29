#ifndef NETWORK_MODULE_H
#define NETWORK_MODULE_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "PubSubClient.h"

extern WiFiClientSecure wifiClient;
extern PubSubClient mqttClient;
extern String mqttCmd;
extern String bleCmd;

void networkInit();
void networkMaintain();
void networkPublish(float waterPercent, float chemPercent, float mixPercent);
bool networkIsConnected();

#endif