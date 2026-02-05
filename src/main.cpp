#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "PCAL9535A.h"
#include "Tank.h"
#include "Relay.h"
#include "cmdline.h"
#include "serial_log.h"
#include "network.h"

// ===== I2C CONFIG =====
#define SDA_PIN 8
#define SCL_PIN 9
#define I2C_FREQ 400000

// ===== GPIO EXPANDER =====
PCAL9535A::PCAL9535A<TwoWire> gpioExpander(Wire);

// ===== HARDWARE OBJECTS =====
Tank waterTank(4);
Tank chemTank(5);
Tank mixTank(6);

// Relay pins on PCAL9535A (P0-P6)
Relay Rwatermain(0, &gpioExpander);
Relay Rmixmain(1, &gpioExpander);
Relay Rwatertomix(2, &gpioExpander);
Relay Rmixtoche(3, &gpioExpander);
Relay Rchetopump(4, &gpioExpander);
Relay Rpumptomix(5, &gpioExpander);
Relay Pump(6, &gpioExpander);

// ===== COMMUNICATION =====
CmdLine cmdLine;

// ===== STATE MACHINE =====
enum SystemMode
{
  MODE_IDLE,           // Không làm gì
  MODE_PROCESS,        // Đang chạy quy trình pha chế (STEP1-4)
  MODE_IRRIGATE_WATER, // Đang tưới nước sạch (V1)
  MODE_IRRIGATE_SOLUTION // Đang tưới dung dịch (V2)
};

enum ProcessStep
{
  STEP_IDLE,
  STEP1_FILL_WATER,
  STEP2_DILUTE_CHEM,
  STEP3_PUMP_CHEM,
  STEP4_MIX
};

// Shared state variables
volatile SystemMode currentMode = MODE_IDLE;
volatile ProcessStep currentStep = STEP_IDLE;
SemaphoreHandle_t modeMutex;

// ===== PROCESS PARAMETERS =====
struct ProcessParams
{
  float totalWater;
  float diluteWater;
  float chemAmount;
  float mixStartVol;
  bool isValid;
};
ProcessParams processParams = {0, 0, 0, 0, false};
SemaphoreHandle_t paramsMutex;

// ===== SENSOR DATA (SHARED) =====
volatile float waterVol = 0;
volatile float chemVol = 0;
volatile float mixVol = 0;
SemaphoreHandle_t sensorMutex;

// ===== TASK HANDLES =====
TaskHandle_t taskCommandHandle = NULL;
TaskHandle_t taskStateMachineHandle = NULL;
TaskHandle_t taskSensorHandle = NULL;
TaskHandle_t taskNetworkHandle = NULL;

// ═════════════════════════════════════════════════════════════
// HELPER FUNCTIONS
// ═════════════════════════════════════════════════════════════
void closeAllValves()
{
  Rwatermain.off();
  Rmixmain.off();
  Rwatertomix.off();
  Rmixtoche.off();
  Rchetopump.off();
  Rpumptomix.off();
}

void stopAll()
{
  closeAllValves();
  Pump.off();
  
  if (xSemaphoreTake(modeMutex, portMAX_DELAY))
  {
    currentMode = MODE_IDLE;
    currentStep = STEP_IDLE;
    xSemaphoreGive(modeMutex);
  }
  
  if (xSemaphoreTake(paramsMutex, portMAX_DELAY))
  {
    processParams.isValid = false;
    xSemaphoreGive(paramsMutex);
  }
}

// ═════════════════════════════════════════════════════════════
// ⭐⭐⭐⭐ TASK 1: COMMAND HANDLER - Priority 4 - Core 0
// Đọc và xử lý commands từ Serial/MQTT
// ═════════════════════════════════════════════════════════════
void TaskCommandHandler(void *pvParameters)
{
  SerialLog::log("[TaskCommand] Started on Core", xPortGetCoreID());

  while (true)
  {
    // ===== XỬ LÝ COMMAND TỪ CMDLINE =====
    if (cmdLine.available())
    {
      String cmd = cmdLine.readStringUntil('\n');
      cmd.trim();
      cmd.toUpperCase();

      if (cmd.startsWith("START "))
      {
        SystemMode localMode;
        if (xSemaphoreTake(modeMutex, pdMS_TO_TICKS(10)))
        {
          localMode = currentMode;
          xSemaphoreGive(modeMutex);
        }

        if (localMode != MODE_IDLE)
        {
          SerialLog::log("❌ Cannot START - System busy");
        }
        else
        {
          float a, b, d;
          if (sscanf(cmd.c_str(), "START %f/%f/%f", &a, &b, &d) == 3)
          {
            if (b > a || a <= 0 || b <= 0 || d <= 0)
            {
              SerialLog::log("❌ Invalid params");
            }
            else
            {
              float currentMixVol;
              if (xSemaphoreTake(sensorMutex, pdMS_TO_TICKS(10)))
              {
                currentMixVol = mixVol;
                xSemaphoreGive(sensorMutex);
              }

              if (xSemaphoreTake(paramsMutex, portMAX_DELAY))
              {
                processParams.totalWater = a;
                processParams.diluteWater = b;
                processParams.chemAmount = d;
                processParams.mixStartVol = currentMixVol;
                processParams.isValid = true;
                xSemaphoreGive(paramsMutex);
              }

              if (xSemaphoreTake(modeMutex, portMAX_DELAY))
              {
                currentMode = MODE_PROCESS;
                currentStep = STEP1_FILL_WATER;
                xSemaphoreGive(modeMutex);
              }

              SerialLog::log("▶ START | W:", a, "L | D:", b, "L | C:", d, "L");
            }
          }
          else
          {
            SerialLog::log("❌ Format: START <total>/<dilute>/<chem>");
          }
        }
      }
      else if (cmd == "V1")
      {
        SystemMode localMode;
        if (xSemaphoreTake(modeMutex, pdMS_TO_TICKS(10)))
        {
          localMode = currentMode;
          xSemaphoreGive(modeMutex);
        }

        if (localMode != MODE_IDLE)
        {
          SerialLog::log("❌ Cannot V1 - System busy");
        }
        else
        {
          if (xSemaphoreTake(modeMutex, portMAX_DELAY))
          {
            currentMode = MODE_IRRIGATE_WATER;
            xSemaphoreGive(modeMutex);
          }
          SerialLog::log("💧 V1: Clean Water");
        }
      }
      else if (cmd == "V2")
      {
        SystemMode localMode;
        if (xSemaphoreTake(modeMutex, pdMS_TO_TICKS(10)))
        {
          localMode = currentMode;
          xSemaphoreGive(modeMutex);
        }

        if (localMode != MODE_IDLE)
        {
          SerialLog::log("❌ Cannot V2 - System busy");
        }
        else
        {
          if (xSemaphoreTake(modeMutex, portMAX_DELAY))
          {
            currentMode = MODE_IRRIGATE_SOLUTION;
            xSemaphoreGive(modeMutex);
          }
          SerialLog::log("🧪 V2: Solution");
        }
      }
      else if (cmd == "STOP")
      {
        stopAll();
        SerialLog::log("⊗ STOP");
      }
      else if (cmd == "STATUS")
      {
        float w, c, m;
        if (xSemaphoreTake(sensorMutex, pdMS_TO_TICKS(10)))
        {
          w = waterVol;
          c = chemVol;
          m = mixVol;
          xSemaphoreGive(sensorMutex);
        }
        
        SystemMode localMode;
        if (xSemaphoreTake(modeMutex, pdMS_TO_TICKS(10)))
        {
          localMode = currentMode;
          xSemaphoreGive(modeMutex);
        }

        SerialLog::log("═══ STATUS ═══");
        SerialLog::log("W:", w, "L | C:", c, "L | M:", m, "L");
        SerialLog::log("Mode:", localMode);
      }
    }

    // ===== ĐỌC SERIAL =====
    if (Serial.available())
    {
      String cmd = Serial.readStringUntil('\n');
      cmd.trim();
      if (cmd.length() > 0)
      {
        cmdLine.println(cmd);
        SerialLog::log("[Serial]", cmd);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// ═════════════════════════════════════════════════════════════
// ⭐⭐⭐ TASK 2: STATE MACHINE - Priority 3 - Core 0
// Điều khiển toàn bộ hardware
// ═════════════════════════════════════════════════════════════
void TaskStateMachine(void *pvParameters)
{
  SerialLog::log("[TaskStateMachine] Started on Core", xPortGetCoreID());

  SystemMode localMode;
  ProcessStep localStep;
  float localWater, localChem, localMix;
  ProcessParams localParams;

  while (true)
  {
    // ĐỌC SHARED DATA
    if (xSemaphoreTake(modeMutex, pdMS_TO_TICKS(10)))
    {
      localMode = currentMode;
      localStep = currentStep;
      xSemaphoreGive(modeMutex);
    }

    if (xSemaphoreTake(sensorMutex, pdMS_TO_TICKS(10)))
    {
      localWater = waterVol;
      localChem = chemVol;
      localMix = mixVol;
      xSemaphoreGive(sensorMutex);
    }

    if (xSemaphoreTake(paramsMutex, pdMS_TO_TICKS(10)))
    {
      localParams = processParams;
      xSemaphoreGive(paramsMutex);
    }

    // ĐIỀU KHIỂN HARDWARE
    switch (localMode)
    {
      case MODE_IDLE:
        closeAllValves();
        Pump.off();
        break;

      case MODE_IRRIGATE_WATER:
        closeAllValves();
        Rwatermain.on();
        Pump.off();
        break;

      case MODE_IRRIGATE_SOLUTION:
        closeAllValves();
        Rmixmain.on();
        Pump.off();
        break;

      case MODE_PROCESS:
      {
        if (!localParams.isValid)
        {
          SerialLog::log("⚠ Invalid params");
          stopAll();
          break;
        }

        switch (localStep)
        {
          case STEP1_FILL_WATER:
          {
            Rwatertomix.on();
            Rwatermain.off();
            Rmixmain.off();
            Rmixtoche.off();
            Rchetopump.off();
            Rpumptomix.off();
            Pump.off();

            if (localMix >= localParams.totalWater)
            {
              closeAllValves();
              SerialLog::log("✓ [STEP1] Filled", localParams.totalWater, "L");
              
              if (xSemaphoreTake(modeMutex, portMAX_DELAY))
              {
                currentStep = STEP2_DILUTE_CHEM;
                xSemaphoreGive(modeMutex);
              }
            }
            break;
          }

          case STEP2_DILUTE_CHEM:
          {
            float waterPumped = localParams.totalWater - localMix;

            Rmixtoche.on();
            Rwatertomix.off();
            Rwatermain.off();
            Rmixmain.off();
            Rchetopump.off();
            Rpumptomix.off();
            Pump.off();

            if (waterPumped >= localParams.diluteWater)
            {
              closeAllValves();
              SerialLog::log("✓ [STEP2] Diluted", localParams.diluteWater, "L");
              
              if (xSemaphoreTake(modeMutex, portMAX_DELAY))
              {
                currentStep = STEP3_PUMP_CHEM;
                xSemaphoreGive(modeMutex);
              }
            }
            break;
          }

          case STEP3_PUMP_CHEM:
          {
            Rchetopump.on();
            Rpumptomix.on();
            Pump.on();
            
            Rwatertomix.off();
            Rwatermain.off();
            Rmixmain.off();
            Rmixtoche.off();

            if (localChem <= 1.0f)
            {
              closeAllValves();
              Pump.off();
              SerialLog::log("✓ [STEP3] Pumped chemical");
              
              if (xSemaphoreTake(modeMutex, portMAX_DELAY))
              {
                currentStep = STEP4_MIX;
                xSemaphoreGive(modeMutex);
              }
            }
            break;
          }

          case STEP4_MIX:
          {
            closeAllValves();
            Pump.on();
            vTaskDelay(pdMS_TO_TICKS(5000));
            Pump.off();

            SerialLog::log("\n═══ COMPLETE ═══");
            SerialLog::log("Mix:", localMix, "L");
            SerialLog::log("Ready: V1 | V2\n");

            if (xSemaphoreTake(modeMutex, portMAX_DELAY))
            {
              currentMode = MODE_IDLE;
              currentStep = STEP_IDLE;
              xSemaphoreGive(modeMutex);
            }

            if (xSemaphoreTake(paramsMutex, portMAX_DELAY))
            {
              processParams.isValid = false;
              xSemaphoreGive(paramsMutex);
            }
            break;
          }

          default:
            break;
        }
        break;
      }

      default:
        break;
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ═════════════════════════════════════════════════════════════
// ⭐⭐ TASK 3: SENSOR READING - Priority 2 - Core 1
// CHỈ đọc cảm biến - KHÔNG có network
// ═════════════════════════════════════════════════════════════
void TaskSensorReading(void *pvParameters)
{
  SerialLog::log("[TaskSensor] Started on Core", xPortGetCoreID());

  while (true)
  {
    // Đọc các tank
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

    vTaskDelay(pdMS_TO_TICKS(200)); // Đọc mỗi 200ms
  }
}

// ═════════════════════════════════════════════════════════════
// ⭐ TASK 4: NETWORK MANAGER - Priority 1 (Lowest) - Core 1
// Xử lý WiFi/MQTT riêng biệt, không ảnh hưởng các task khác
// ═════════════════════════════════════════════════════════════
void TaskNetworkManager(void *pvParameters)
{
  SerialLog::log("[TaskNetwork] Started on Core", xPortGetCoreID());
  
  // Delay 3 giây để các task khác khởi động trước
  vTaskDelay(pdMS_TO_TICKS(3000));
  
  SerialLog::log("🌐 Initializing Network...");
  
  // Non-blocking network init với timeout
  networkInit();
  
  unsigned long lastPublish = 0;
  unsigned long lastMaintain = 0;

  while (true)
  {
    unsigned long currentMillis = millis();

    // ===== NETWORK MAINTAIN (mỗi 1 giây) =====
    if (currentMillis - lastMaintain >= 1000)
    {
      lastMaintain = currentMillis;
      networkMaintain(); // Gọi ít hơn để tránh spam WiFi stack
    }

    // ===== PUBLISH DATA (mỗi 30 giây) =====
    if (currentMillis - lastPublish >= 30000)
    {
      lastPublish = currentMillis;
      
      // Đọc sensor data
      float w, c, m;
      if (xSemaphoreTake(sensorMutex, pdMS_TO_TICKS(10)))
      {
        w = waterVol;
        c = chemVol;
        m = mixVol;
        xSemaphoreGive(sensorMutex);
      }
      
      // Tính %
      float wPercent = (w / 500.0f) * 100.0f;
      float cPercent = (c / 100.0f) * 100.0f;
      float mPercent = (m / 500.0f) * 100.0f;
      
      // Publish (non-blocking)
      networkPublish(wPercent, cPercent, mPercent);
     // SerialLog::log("📡 Published");
    }

    vTaskDelay(pdMS_TO_TICKS(500)); // 500ms cycle - không cần nhanh
  }
}

// ═════════════════════════════════════════════════════════════
// SETUP & LOOP
// ═════════════════════════════════════════════════════════════
void setup()
{
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n╔═════════════════════════════════════════╗");
  Serial.println("║   PUMP CONTROLLER - 4 TASKS VERSION    ║");
  Serial.println("╚═════════════════════════════════════════╝");
  Serial.flush();

  // ===== INIT HARDWARE =====
  SerialLog::log("🔧 Initializing I2C...");
  Wire.begin(SDA_PIN, SCL_PIN, I2C_FREQ);
  delay(100);
  
  SerialLog::log("🔧 Initializing GPIO Expander...");
  gpioExpander.begin();
  delay(100);
  
  SerialLog::log("🔧 Configuring relays...");
  Rwatermain.begin();
  Rmixmain.begin();
  Rwatertomix.begin();
  Rmixtoche.begin();
  Rchetopump.begin();
  Rpumptomix.begin();
  Pump.begin();
  SerialLog::log("✅ Hardware OK\n");

  // ===== COMMANDS =====
  SerialLog::log("Commands: START a/b/d | V1 | V2 | STOP | STATUS\n");

  // ===== MUTEXES =====
  modeMutex = xSemaphoreCreateMutex();
  paramsMutex = xSemaphoreCreateMutex();
  sensorMutex = xSemaphoreCreateMutex();

  SerialLog::log("🚀 Creating 4 tasks...\n");

  // Task 1: Command (P4) - Core 0
  xTaskCreatePinnedToCore(
      TaskCommandHandler, "Command", 10240, NULL, 4, &taskCommandHandle, 0);
  SerialLog::log("  ✓ Task 1: Command (P4) - Core 0");

  // Task 2: State Machine (P3) - Core 0
  xTaskCreatePinnedToCore(
      TaskStateMachine, "StateMachine", 10240, NULL, 3, &taskStateMachineHandle, 0);
  SerialLog::log("  ✓ Task 2: StateMachine (P3) - Core 0");

  // Task 3: Sensor (P2) - Core 1
  xTaskCreatePinnedToCore(
      TaskSensorReading, "Sensor", 4096, NULL, 2, &taskSensorHandle, 1);
  SerialLog::log("  ✓ Task 3: Sensor (P2) - Core 1");

  // Task 4: Network (P1) - Core 1
  xTaskCreatePinnedToCore(
      TaskNetworkManager, "Network", 8192, NULL, 1, &taskNetworkHandle, 1);
  SerialLog::log("  ✓ Task 4: Network (P1) - Core 1");

  SerialLog::log("\n✅ System ready!\n");
}

void loop()
{
  vTaskDelay(portMAX_DELAY);
}