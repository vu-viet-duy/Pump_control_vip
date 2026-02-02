#include "Relay.h"

Relay::Relay(uint8_t p, PCAL9535A::PCAL9535A<TwoWire>* gpioExpander)
    : pin(p), state(false), gpio(gpioExpander)
{
    // Pin will be initialized in begin()
}

void Relay::begin()
{
    if (gpio != nullptr)
    {
        gpio->pinMode(pin, OUTPUT);
        gpio->digitalWrite(pin, LOW); // OFF (relay active LOW)
    }
}

void Relay::on()
{
    if (gpio != nullptr)
    {
        gpio->digitalWrite(pin, HIGH);
        state = true;
    }
}

void Relay::off()
{
    if (gpio != nullptr)
    {
        gpio->digitalWrite(pin, LOW);
        state = false;
    }
}

void Relay::getstate()
{
    Serial.print("Relay Pin ");
    Serial.print(pin);
    Serial.print(": ");
    Serial.println(state ? "ON" : "OFF");
}
