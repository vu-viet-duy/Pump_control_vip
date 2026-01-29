#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <Arduino.h>

class CommandHandler
{
public:
    CommandHandler();

    void begin(void (*cb)(const String &));

    void onSerial();
    void onMQTT(const String &msg);
    void onBLE(const String &msg);

private:
    void onMessage(const String &msg);
    void (*callback)(const String &);
};

#endif
