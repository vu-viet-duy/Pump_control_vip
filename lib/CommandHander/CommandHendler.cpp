#include <Arduino.h>
#include "CommandHandler.h"

CommandHandler::CommandHandler()
{
    callback = nullptr;
}

void CommandHandler::begin(void (*cb)(const String &))
{
    callback = cb;
}

void CommandHandler::onSerial()
{
    if (!Serial.available())
        return;

    String msg = Serial.readStringUntil('\n');
    onMessage(msg);
}

void CommandHandler::onMQTT(const String &msg)
{
    onMessage(msg);
}

void CommandHandler::onBLE(const String &msg)
{
    onMessage(msg);
}

void CommandHandler::onMessage(const String &msg)
{
    String cmd = msg;
    cmd.trim();
    if (cmd.length() == 0)
        return;
    if (callback)
        callback(cmd);
}
