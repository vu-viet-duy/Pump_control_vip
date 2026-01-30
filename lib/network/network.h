#ifndef NETWORK_MODULE_H
#define NETWORK_MODULE_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "PubSubClient.h"
#include <cmdline.h>

extern WiFiClientSecure wifiClient;
extern PubSubClient mqttClient;
extern CmdLine cmdLine; // ⭐ Để mqttCallback push trực tiếp vào queue

void networkInit();
void networkMaintain();
void networkPublish(float waterPercent, float chemPercent, float mixPercent);
bool networkIsConnected();

#endif