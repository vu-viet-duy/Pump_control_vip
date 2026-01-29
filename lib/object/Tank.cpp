#include "Tank.h"

Tank::Tank(uint8_t p)
    : pin(p), maxvol(1000.0f), currentVol(0.0f)
{
    pinMode(pin, INPUT);
}

float Tank::getValue()
{
    int rawValue = analogRead(pin);
    currentVol = map(rawValue, 0, 4095, 0, maxvol);
    return currentVol;
}

float Tank::getMax()
{
    return maxvol;
}

float Tank::getPercentage()
{
    if (maxvol <= 0.0f)
        return 0.0f;
    return (currentVol / maxvol) * 100.0f;
}