#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#define STR_LEN 64  // ✅ FIXED: Increased from 16 to 64
#define QUEUE_LENGTH 10

class CmdLine
{
private:
    QueueHandle_t cmdQueue;

public:
    CmdLine()
    {
        cmdQueue = xQueueCreate(QUEUE_LENGTH, STR_LEN);
        if (cmdQueue == NULL)
        {
            ESP_LOGE("CmdLine", "Failed to create queue!");
        }
    }
    ~CmdLine()
    {
        if (cmdQueue != NULL)
        {
            vQueueDelete(cmdQueue);
        }
    }

    String bufferData;

    bool println(const String &command)
    {
        if (cmdQueue == NULL)
        {
            return false;
        }

        char buffer[STR_LEN] = {0};
        size_t copyLen = command.length() < STR_LEN - 2 ? command.length() : STR_LEN - 2;
        command.toCharArray(buffer, copyLen + 1);

        size_t len = strlen(buffer);
        if (len < STR_LEN - 1)
        {
            buffer[len] = '\n';
            buffer[len + 1] = '\0';
        }
        else
        {
            buffer[STR_LEN - 2] = '\n';
            buffer[STR_LEN - 1] = '\0';
        }

        return xQueueSend(cmdQueue, buffer, portMAX_DELAY) == pdPASS;
    }

    bool available()
    {
        if (cmdQueue == NULL)
        {
            return false;
        }
        return uxQueueMessagesWaiting(cmdQueue) > 0;
    }

    String readStringUntil(char terminator = '\n')
    {
        if (cmdQueue == NULL || !available())
        {
            return "";
        }

        char buffer[STR_LEN] = {0};
        if (xQueueReceive(cmdQueue, buffer, 0) == pdPASS)
        {
            // Tìm và thay thế terminator bằng null terminator
            for (int i = 0; i < STR_LEN && buffer[i] != '\0'; i++)
            {
                if (buffer[i] == terminator)
                {
                    buffer[i] = '\0';
                    break;
                }
            }
            return String(buffer);
        }
        return "";
    }

    String readString()
    {
        if (cmdQueue == NULL || !available())
        {
            return "";
        }

        char buffer[STR_LEN] = {0};
        if (xQueueReceive(cmdQueue, buffer, 0) == pdPASS)
        {
            return String(buffer);
        }
        return "";
    }

    void flush()
    {
        if (cmdQueue != NULL)
        {
            xQueueReset(cmdQueue);
        }
    }

    bool isValid() const
    {
        return cmdQueue != NULL;
    }
    CmdLine(const CmdLine &) = delete;
    CmdLine &operator=(const CmdLine &) = delete;
};