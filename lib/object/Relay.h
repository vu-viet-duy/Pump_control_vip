
#include <Arduino.h>

class Relay
{
private:
    String name;
    uint8_t pin;
    bool state; // true = ON, false = OFF
public:
    Relay(uint8_t p);
    void on();
    void off();
    void getstate();
    void openfortime();
};
