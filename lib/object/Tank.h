#include <Arduino.h>
class Tank
{
private:
    uint8_t pin;
    float maxvol;
    float currentVol;

public:
    Tank(uint8_t p);
    float getValue();      // dung tích hiện tại
    float getMax();        // dung tích tối đa
    float getPercentage(); // %
};
