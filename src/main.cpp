#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "Tank.h"
#include "Relay.h"
#include "cmdline.h"
#include "serial_log.h"
#include "network.h"

// ===== HARDWARE OBJECTS =====
Tank waterTank(4);
Tank chemTank(5);
Tank mixTank(6);

Relay Rwatermain(7);
Relay Rmixmain(8);
Relay Rwatertomix(9);
Relay Rmixtoche(10);
Relay Rchetopump(11);
Relay Rpumptomix(12);
Relay Pump(13);

// ===== COMMUNICATION =====
CmdLine cmdLine;

// ===== STATE MACHINE =====
enum Step
{
  IDLE,
  STEP1,
  STEP2,
  STEP3,
  STEP4,
  IRRIGATE_WATER,
  IRRIGATE_SOLUTION
};

volatile Step currentStep = IDLE;
SemaphoreHandle_t stepMutex;

// ===== PROCESS PARAMETERS =====
struct ProcessParams
{
  float totalWater;
  float diluteWater;
  float chemAmount;
  float mixStartVol;
};
ProcessParams processParams = {0, 0, 0, 0};
SemaphoreHandle_t paramsMutex;

// ===== SENSOR DATA (SHARED) =====
volatile float waterVol = 0;
volatile float chemVol = 0;
volatile float mixVol = 0;
SemaphoreHandle_t sensorMutex;

// ===== TASK HANDLES =====
TaskHandle_t taskCommandHandle = NULL;
TaskHandle_t taskControlHandle = NULL;
TaskHandle_t taskProcessHandle = NULL;
TaskHandle_t taskSensorHandle = NULL;
TaskHandle_t taskNetworkHandle = NULL;

// ===== HELPER FUNCTIONS =====
void closeAllValves()
{
  Rwatermain.off();
  Rmixmain.off();
  Rwatertomix.off();
  Rmixtoche.off();
  Rchetopump.off();
  Rpumptomix.off();
}

// ═════════════════════════════════════════════════════════════
// ⭐⭐⭐⭐ TASK 1: CONTROL - Priority 4 (Highest)
// ═════════════════════════════════════════════════════════════
void TaskControl(void *pvParameters)
{
  SerialLog::log("[TaskControl] Started on Core", xPortGetCoreID());

  while (true)
  {
    // Kiểm tra có lệnh mới từ queue không
    if (cmdLine.available())
    {
      String cmd = cmdLine.readStringUntil('\n');
      cmd.trim();
      cmd.toUpperCase();

      if (cmd.startsWith("START "))
      {
        float a, b, d;
        if (sscanf(cmd.c_str(), "START %f/%f/%f", &a, &b, &d) == 3)
        {
          if (b > a || a <= 0 || b <= 0 || d <= 0)
          {
            SerialLog::log("❌ Invalid parameters");
          }
          else
          {
            // Cập nhật parameters
            if (xSemaphoreTake(paramsMutex, portMAX_DELAY))
            {
              processParams.totalWater = a;
              processParams.diluteWater = b;
              processParams.chemAmount = d;
              
              if (xSemaphoreTake(sensorMutex, pdMS_TO_TICKS(10)))
              {
                processParams.mixStartVol = mixVol;
                xSemaphoreGive(sensorMutex);
              }
              xSemaphoreGive(paramsMutex);
            }

            // Chuyển sang STEP1
            if (xSemaphoreTake(stepMutex, portMAX_DELAY))
            {
              currentStep = STEP1;
              xSemaphoreGive(stepMutex);
            }

            SerialLog::log("▶ START | Water:", a, "L | Dilute:", b, "L | Chem:", d, "L");
          }
        }
        else
        {
          SerialLog::log("❌ Format: START <total>/<dilute>/<chem>");
        }
      }
      else if (cmd == "V1")
      {
        if (xSemaphoreTake(stepMutex, portMAX_DELAY))
        {
          currentStep = IRRIGATE_WATER;
          xSemaphoreGive(stepMutex);
        }
        SerialLog::log("→ Irrigate clean water");
      }
      else if (cmd == "V2")
      {
        if (xSemaphoreTake(stepMutex, portMAX_DELAY))
        {
          currentStep = IRRIGATE_SOLUTION;
          xSemaphoreGive(stepMutex);
        }
        SerialLog::log("→ Irrigate solution");
      }
      else if (cmd == "STOP")
      {
        if (xSemaphoreTake(stepMutex, portMAX_DELAY))
        {
          currentStep = IDLE;
          xSemaphoreGive(stepMutex);
        }
        closeAllValves();
        Pump.off();
        SerialLog::log("⊗ STOP");
      }
      else if (cmd == "STATUS")
      {
        if (xSemaphoreTake(sensorMutex, pdMS_TO_TICKS(10)))
        {
          SerialLog::log("Water:", waterVol, "L | Chem:", chemVol, "L | Mix:", mixVol, "L");
          xSemaphoreGive(sensorMutex);
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(50)); // 50ms cycle
  }
}

// ═════════════════════════════════════════════════════════════
// ⭐⭐⭐ TASK 2: COMMAND - Priority 3
// Thu thập lệnh từ các nguồn LOCAL (Serial, BLE, Button...)
// MQTT push TRỰC TIẾP vào queue (trong mqttCallback)
// ═════════════════════════════════════════════════════════════
void TaskCommand(void *pvParameters)
{
  SerialLog::log("[TaskCommand] Started on Core", xPortGetCoreID());

  while (true)
  {
    // ===== NGUỒN 1: SERIAL =====
    if (Serial.available())
    {
      String cmd = Serial.readStringUntil('\n');
      cmd.trim();
      if (cmd.length() > 0)
      {
        cmdLine.println(cmd);
        SerialLog::log("[CMD:Serial]", cmd);
      }
    }

    // ===== NGUỒN 2: BLE =====
    // TODO: Thêm BLE command handler ở đây
    // if (BLE.available()) {
    //   String cmd = BLE.readStringUntil('\n');
    //   cmdLine.println(cmd);
    //   SerialLog::log("[CMD:BLE]", cmd);
    // }

    // ===== NGUỒN 3: BUTTON/GPIO =====
    // TODO: Đọc button physical
    // if (digitalRead(BUTTON_START) == LOW) {
    //   cmdLine.println("START 400/50/10");
    //   SerialLog::log("[CMD:Button] START");
    //   vTaskDelay(pdMS_TO_TICKS(500)); // Debounce
    // }

    vTaskDelay(pdMS_TO_TICKS(10)); // 10ms cycle
  }
}

// ═════════════════════════════════════════════════════════════
// ⭐⭐⭐ TASK 3: PROCESS - Priority 3
// ═════════════════════════════════════════════════════════════
void TaskProcess(void *pvParameters)
{
  SerialLog::log("[TaskProcess] Started on Core", xPortGetCoreID());

  Step localStep;
  float localWaterVol, localChemVol, localMixVol;
  ProcessParams localParams;

  while (true)
  {
    // Đọc current step
    if (xSemaphoreTake(stepMutex, pdMS_TO_TICKS(10)))
    {
      localStep = currentStep;
      xSemaphoreGive(stepMutex);
    }

    // Đọc sensor data
    if (xSemaphoreTake(sensorMutex, pdMS_TO_TICKS(10)))
    {
      localWaterVol = waterVol;
      localChemVol = chemVol;
      localMixVol = mixVol;
      xSemaphoreGive(sensorMutex);
    }

    // Đọc parameters
    if (xSemaphoreTake(paramsMutex, pdMS_TO_TICKS(10)))
    {
      localParams = processParams;
      xSemaphoreGive(paramsMutex);
    }

    // State machine
    switch (localStep)
    {
    case IDLE:
      closeAllValves();
      Pump.off();
      break;

    case STEP1: // Bơm nước vào Mix
    {
      Rwatertomix.on();
      Rwatermain.off();
      Rmixtoche.off();
      Rchetopump.off();
      Rpumptomix.off();
      Rmixmain.off();

      if (localMixVol >= localParams.totalWater)
      {
        closeAllValves();
        SerialLog::log("✓ Filled Mix with", localParams.totalWater, "L");
        SerialLog::log(">>> STEP 2: Dilute chemical...");
        
        if (xSemaphoreTake(stepMutex, portMAX_DELAY))
        {
          currentStep = STEP2;
          xSemaphoreGive(stepMutex);
        }
      }
      else
      {
        SerialLog::log("[STEP1] Filling |", localMixVol, "L /", localParams.totalWater, "L");
      }
      break;
    }

    case STEP2: // Pha loãng hóa chất
    {
      float waterPumped = localParams.totalWater - localMixVol;

      Rmixtoche.on();
      Rwatertomix.off();
      Rwatermain.off();
      Rchetopump.off();
      Rpumptomix.off();
      Rmixmain.off();
      Pump.off();

      if (waterPumped >= localParams.chemAmount)
      {
        closeAllValves();
        SerialLog::log("✓ Diluted with", localParams.chemAmount, "L water");
        SerialLog::log(">>> STEP 3: Pump chemical...");
        
        if (xSemaphoreTake(stepMutex, portMAX_DELAY))
        {
          currentStep = STEP3;
          xSemaphoreGive(stepMutex);
        }
      }
      else
      {
        SerialLog::log("[STEP2] Diluting |", waterPumped, "L /", localParams.chemAmount, "L");
      }
      break;
    }

    case STEP3: // Bơm hóa chất vào Mix
    {
      Rchetopump.on();
      Rpumptomix.on();
      Pump.on();
      Rwatertomix.off();
      Rwatermain.off();
      Rmixtoche.off();
      Rmixmain.off();

      if (localChemVol <= 1.0f)
      {
        closeAllValves();
        Pump.off();
        SerialLog::log("✓ Pumped all chemical to Mix");
        SerialLog::log(">>> STEP 4: Mixing...");
        
        if (xSemaphoreTake(stepMutex, portMAX_DELAY))
        {
          currentStep = STEP4;
          xSemaphoreGive(stepMutex);
        }
      }
      else
      {
        SerialLog::log("[STEP3] Pumping | Chem:", localChemVol, "L");
      }
      break;
    }

    case STEP4: // Khuấy trộn
    {
      closeAllValves();
      Pump.on();

      SerialLog::log("\n========== RESULT ==========");
      SerialLog::log("📦 Mix:", localMixVol, "L");
      SerialLog::log("💧 Water:", localParams.totalWater, "L");
      SerialLog::log("🧪 Chemical:", localParams.chemAmount, "L");
      SerialLog::log("============================");
      SerialLog::log("✓ Complete! Ready: V1 | V2");

      if (xSemaphoreTake(stepMutex, portMAX_DELAY))
      {
        currentStep = IDLE;
        xSemaphoreGive(stepMutex);
      }
      break;
    }

    case IRRIGATE_WATER:
      closeAllValves();
      Rwatermain.on();
      break;

    case IRRIGATE_SOLUTION:
      closeAllValves();
      Rmixmain.on();
      break;
    }

    vTaskDelay(pdMS_TO_TICKS(100)); // 100ms cycle
  }
}
// ═════════════════════════════════════════════════════════════
// ⭐⭐ TASK 4: SENSOR - Priority 2
// ═════════════════════════════════════════════════════════════
void TaskSensor(void *pvParameters)
{
  SerialLog::log("[TaskSensor] Started on Core", xPortGetCoreID());

  while (true)
  {
    // Đọc các cảm biến
    float w = waterTank.getValue();
    float c = chemTank.getValue();
    float m = mixTank.getValue();

    // Cập nhật shared variables
    if (xSemaphoreTake(sensorMutex, pdMS_TO_TICKS(10)))
    {
      waterVol = w;
      chemVol = c;
      mixVol = m;
      xSemaphoreGive(sensorMutex);
    }

    vTaskDelay(pdMS_TO_TICKS(200)); // 200ms cycle
  }
}

// ═════════════════════════════════════════════════════════════
// ⭐ TASK 5: NETWORK - Priority 1 (Lowest)
// ═════════════════════════════════════════════════════════════
void TaskNetwork(void *pvParameters)
{
  SerialLog::log("[TaskNetwork] Started on Core", xPortGetCoreID());
  
  // Init network
  networkInit();
  
  unsigned long lastPublish = 0;

  while (true)
  {
    // Maintain WiFi/MQTT
    networkMaintain();

    // Publish every 5 seconds
    if (millis() - lastPublish >= 5000)
    {
      lastPublish = millis();
      
      float w, c, m;
      if (xSemaphoreTake(sensorMutex, pdMS_TO_TICKS(10)))
      {
        w = waterVol;
        c = chemVol;
        m = mixVol;
        xSemaphoreGive(sensorMutex);
      }
      
      // Tính % (giả sử max = 500L)
      float wPercent = (w / 500.0f) * 100.0f;
      float cPercent = (c / 100.0f) * 100.0f;
      float mPercent = (m / 500.0f) * 100.0f;
      
      networkPublish(wPercent, cPercent, mPercent);
    }

    vTaskDelay(pdMS_TO_TICKS(100)); // 100ms cycle
  }
}

// ═════════════════════════════════════════════════════════════
// SETUP & LOOP
// ═════════════════════════════════════════════════════════════
void setup()
{
  Serial.begin(115200);
  delay(1000);

  SerialLog::log("\n========================================");
  SerialLog::log("     PUMP CONTROLLER - RTOS Version     ");
  SerialLog::log("========================================");
  SerialLog::log("\nCommands:");
  SerialLog::log("  START <total>/<dilute>/<chem>");
  SerialLog::log("  V1     - Irrigate clean water");
  SerialLog::log("  V2     - Irrigate solution");
  SerialLog::log("  STOP   - Stop all");
  SerialLog::log("  STATUS - Show status\n");

  // Tạo mutexes
  stepMutex = xSemaphoreCreateMutex();
  paramsMutex = xSemaphoreCreateMutex();
  sensorMutex = xSemaphoreCreateMutex();

  SerialLog::log("Creating RTOS tasks...\n");

  // Tạo tasks với priority theo yêu cầu
  xTaskCreatePinnedToCore(
      TaskControl,
      "TaskControl",
      4096,
      NULL,
      4, // ⭐⭐⭐⭐ Priority 4 - Highest
      &taskControlHandle,
      0 // Core 0
  );

  xTaskCreatePinnedToCore(
      TaskCommand,
      "TaskCommand",
      2048,
      NULL,
      3, // ⭐⭐⭐ Priority 3
      &taskCommandHandle,
      0 // Core 0
  );

  xTaskCreatePinnedToCore(
      TaskProcess,
      "TaskProcess",
      4096,
      NULL,
      3, // ⭐⭐⭐ Priority 3
      &taskProcessHandle,
      1 // Core 1
  );

  xTaskCreatePinnedToCore(
      TaskSensor,
      "TaskSensor",
      2048,
      NULL,
      2, // ⭐⭐ Priority 2
      &taskSensorHandle,
      1 // Core 1
  );

  xTaskCreatePinnedToCore(
      TaskNetwork,
      "TaskNetwork",
      8192,
      NULL,
      1, // ⭐ Priority 1 - Lowest
      &taskNetworkHandle,
      0 // Core 0
  );

  SerialLog::log("✓ All tasks created!");
  SerialLog::log("========================================\n");
}

void loop()
{
  // FreeRTOS đang chạy, loop() không cần làm gì
  vTaskDelay(portMAX_DELAY);
}