#ifndef RELAY_H
#define RELAY_H

#include <Arduino.h>
#include <Wire.h>
#include "PCAL9535A.h"

class Relay
{
private:
    String name;
    uint8_t pin;
    bool state; // true = ON, false = OFF
    PCAL9535A::PCAL9535A<TwoWire>* gpio; // Pointer to shared GPIO expander
public:
    Relay(uint8_t p, PCAL9535A::PCAL9535A<TwoWire>* gpioExpander);
    void begin(); // Initialize pin as output
    void on();
    void off();
    void getstate();
    void openfortime();
};

#endif
