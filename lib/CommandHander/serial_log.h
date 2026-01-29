#pragma once
#include <Arduino.h>

class SerialLog
{
public:
    static constexpr size_t BUF_SIZE = 512;
    template <typename... Args>
    static void log(Args... args)
    {
#ifdef DEBUG
        char buf[BUF_SIZE];
        auto str = toString(args...);
        int len = snprintf(buf, sizeof(buf), "%s", str.c_str());
        if (len > 0)
        {
            Serial.write((uint8_t *)buf, len);
            Serial.write('\n');
        }
#endif
    }

private:
    template <typename T>
    static String toString(T value)
    {
        return String(value);
    }

    static String toString(float value)
    {
        return String(value, 7); // ép float 7 số sau dấu .
    }

    static String toString(double value)
    {
        return String(value, 7); // ép double 7 số sau dấu .
    }

    template <typename T, typename... Args>
    static String toString(T value, Args... args)
    {
        return toString(value) + " " + toString(args...);
    }
};
