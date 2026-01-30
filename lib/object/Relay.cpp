#include "Relay.h"


Relay::Relay(uint8_t p)
    : pin(p), state(false)
{
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH); // OFF
}

void Relay::on()
{
    digitalWrite(pin, LOW);
    state = true;
}

void Relay::off()
{
    digitalWrite(pin, HIGH);
    state = false;
}

void Relay::getstate()
{
    Serial.print("Relay: ");
    Serial.println(state ? "ON" : "OFF");
}
