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
Tank waterTank(4);    // Water tank sensor
Tank chemTank(5);     // Chemical tank sensor
Tank mixTank(6);      // Mix tank sensor

// Relay pins on PCAL9535A (P0-P6)
Relay Rwatermain(0, &gpioExpander);
Relay Rmixmain(1, &gpioExpander);
Relay Rwatertomix(2, &gpioExpander);
Relay Rmixtoche(3, &gpioExpander);
Relay Rchetopump(4, &gpioExpander);
Relay Rpumptomix(5, &gpioExpander);
Relay Pump(6, &gpioExpander);

// ===== COMMUNICATION =====
CmdLine cmdLine;  // Unified command queue (Serial/MQTT/BLE)

// ===== STATE MACHINE =====
enum SystemMode {
    MODE_IDLE,             // System idle
    MODE_PROCESS,          // Running mixing process
    MODE_IRRIGATE_WATER,   // Clean water irrigation (V1)
    MODE_IRRIGATE_SOLUTION // Solution irrigation (V2)
};

enum ProcessStep {
    STEP_IDLE,
    STEP1_FILL_WATER,      // Fill water to mix tank
    STEP2_DILUTE_CHEM,     // Dilute chemical in chem tank
    STEP3_PUMP_CHEM,       // Pump diluted chemical to mix
    STEP4_MIX              // Final mixing
};

// ===== PROCESS PARAMETERS =====
struct ProcessParams {
    float totalWater;      // Total water needed (L)
    float diluteWater;     // Water for dilution (L)
    float chemAmount;      // Chemical solution amount (L)
    bool isValid;          // Parameters validation flag
};

// ===== GLOBAL STATE =====
SystemMode currentMode = MODE_IDLE;
ProcessStep currentStep = STEP_IDLE;
ProcessParams processParams = {0, 0, 0, false};

// Single mutex for all shared state (simplified)
SemaphoreHandle_t stateMutex;

// ===== TASK HANDLES =====
TaskHandle_t taskCommandHandle = NULL;
TaskHandle_t taskControlHandle = NULL;
TaskHandle_t taskNetworkHandle = NULL;

// ═════════════════════════════════════════════════════════════
// HELPER FUNCTIONS
// ═════════════════════════════════════════════════════════════
void closeAllValves() {
    Rwatermain.off();
    Rmixmain.off();
    Rwatertomix.off();
    Rmixtoche.off();
    Rchetopump.off();
    Rpumptomix.off();
}

void stopAll() {
    closeAllValves();
    Pump.off();
    
    if (xSemaphoreTake(stateMutex, portMAX_DELAY)) {
        currentMode = MODE_IDLE;
        currentStep = STEP_IDLE;
        processParams.isValid = false;
        xSemaphoreGive(stateMutex);
    }
    
    SerialLog::log("⛔ All systems stopped");
}

void printProcessSummary(float water, float chem, float mix) {
    SerialLog::log("\n══════════════════════════════════════════");
    SerialLog::log("✅ PROCESS COMPLETE!");
    SerialLog::log("══════════════════════════════════════════");
    SerialLog::log("📊 FINAL TANK LEVELS:");
    SerialLog::log("   💧 Water tank:", water, "L");
    SerialLog::log("   🧪 Chemical tank:", chem, "L");
    SerialLog::log("   🌀 Mix tank:", mix, "L");
    SerialLog::log("\n📋 RECIPE EXECUTED:");
    SerialLog::log("   Total water:", processParams.totalWater, "L");
    SerialLog::log("   Dilution water:", processParams.diluteWater, "L");
    SerialLog::log("   Chemical amount:", processParams.chemAmount, "L");
    SerialLog::log("\n🚿 READY FOR IRRIGATION:");
    SerialLog::log("   V1 - Clean water | V2 - Solution");
}

// ═════════════════════════════════════════════════════════════
// TASK 1: COMMAND HANDLER (Priority 4 - Core 0)
// Processes commands from Serial/MQTT/BLE via cmdLine queue
// ═════════════════════════════════════════════════════════════
void TaskCommandHandler(void *pvParameters) {
    SerialLog::log("[CMD] Task started on Core", xPortGetCoreID());

    while (true) {
        // Process commands from unified queue
        if (cmdLine.available()) {
            String cmd = cmdLine.readStringUntil('\n');
            cmd.trim();
            cmd.toUpperCase();

            SerialLog::log("[CMD] Processing:", cmd);

            if (cmd.startsWith("START ")) {
                float totalWater, diluteWater, chemAmount;
                
                if (sscanf(cmd.c_str(), "START %f/%f/%f", 
                          &totalWater, &diluteWater, &chemAmount) == 3) {
                    
                    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50))) {
                        bool canStart = (currentMode == MODE_IDLE && 
                                       diluteWater <= totalWater && 
                                       totalWater > 0 && diluteWater > 0 && chemAmount > 0);
                        
                        if (canStart) {
                            processParams.totalWater = totalWater;
                            processParams.diluteWater = diluteWater;
                            processParams.chemAmount = chemAmount;
                            processParams.isValid = true;
                            currentMode = MODE_PROCESS;
                            currentStep = STEP1_FILL_WATER;
                            
                            SerialLog::log("▶ START confirmed:");
                            SerialLog::log("   Water:", totalWater, "L");
                            SerialLog::log("   Dilute:", diluteWater, "L");
                            SerialLog::log("   Chemical:", chemAmount, "L");
                        } else {
                            SerialLog::log("❌ Cannot START - System busy or invalid params");
                        }
                        xSemaphoreGive(stateMutex);
                    }
                } else {
                    SerialLog::log("❌ Invalid format. Use: START 500/50/300");
                }
            }
            else if (cmd == "V1") {
                if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50))) {
                    if (currentMode == MODE_IDLE) {
                        currentMode = MODE_IRRIGATE_WATER;
                        SerialLog::log("💧 V1: Clean water irrigation");
                    } else {
                        SerialLog::log("❌ System busy");
                    }
                    xSemaphoreGive(stateMutex);
                }
            }
            else if (cmd == "V2") {
                if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50))) {
                    if (currentMode == MODE_IDLE) {
                        currentMode = MODE_IRRIGATE_SOLUTION;
                        SerialLog::log("🧪 V2: Solution irrigation");
                    } else {
                        SerialLog::log("❌ System busy");
                    }
                    xSemaphoreGive(stateMutex);
                }
            }
            else if (cmd == "STOP") {
                stopAll();
            }
            else if (cmd == "STATUS") {
                // Control task will handle STATUS with real-time sensor data
                SerialLog::log("[STATUS] Requested - see Control task logs");
            }
        }

        // Forward Serial input to command queue
        if (Serial.available()) {
            String input = Serial.readStringUntil('\n');
            input.trim();
            if (input.length() > 0) {
                cmdLine.println(input);
                SerialLog::log("[Serial]", input);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));  // 20Hz
    }
}

// ═════════════════════════════════════════════════════════════
// TASK 2: CONTROL & SENSOR COMBINED (Priority 3 - Core 0)
// Reads sensors directly + controls hardware + handles STATUS
// ═════════════════════════════════════════════════════════════
void TaskControl(void *pvParameters) {
    SerialLog::log("[CTRL] Task started on Core", xPortGetCoreID());

    unsigned long lastStatusLog = 0;
    unsigned long lastStepLog = 0;
    unsigned long mixStartTime = 0;
    bool mixing = false;

    while (true) {
        // DIRECT SENSOR READING (real-time, no shared variables)
        float waterLevel = waterTank.getValue();   // Read directly
        float chemLevel = chemTank.getValue();     // Read directly
        float mixLevel = mixTank.getValue();       // Read directly

        // Read current state (protected by mutex)
        SystemMode localMode;
        ProcessStep localStep;
        ProcessParams localParams;
        
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10))) {
            localMode = currentMode;
            localStep = currentStep;
            localParams = processParams;
            xSemaphoreGive(stateMutex);
        }

        // Handle STATUS command (real-time data)
        if (millis() - lastStatusLog >= 200) {  // Auto-status every 200ms
            lastStatusLog = millis();
            SerialLog::log("[STATUS]", 
                          "W:", waterLevel, "L | ",
                          "C:", chemLevel, "L | ",
                          "M:", mixLevel, "L | ",
                          "Mode:", localMode);
        }

        // STATE MACHINE CONTROL
        switch (localMode) {
            case MODE_IDLE:
                closeAllValves();
                Pump.off();
                mixing = false;
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
                if (!localParams.isValid) {
                    SerialLog::log("⚠ Invalid process parameters");
                    stopAll();
                    break;
                }

                // Step progress logging
                if (millis() - lastStepLog >= 2000) {
                    lastStepLog = millis();
                    SerialLog::log("[PROCESS]", "Step:", localStep, 
                                  " | Mix:", mixLevel, "L",
                                  " | Chem:", chemLevel, "L");
                }

                switch (localStep) {
                    case STEP1_FILL_WATER:
                        Rwatertomix.on();
                        Pump.off();
                        
                        if (mixLevel >= localParams.totalWater) {
                            closeAllValves();
                            if (xSemaphoreTake(stateMutex, portMAX_DELAY)) {
                                currentStep = STEP2_DILUTE_CHEM;
                                xSemaphoreGive(stateMutex);
                            }
                            SerialLog::log("✅ STEP1 Complete - Water filled");
                        }
                        break;

                    case STEP2_DILUTE_CHEM:
                    {
                        Rmixtoche.on();
                        Pump.off();
                        
                        float waterTransferred = localParams.totalWater - mixLevel;
                        if (waterTransferred >= localParams.diluteWater) {
                            closeAllValves();
                            if (xSemaphoreTake(stateMutex, portMAX_DELAY)) {
                                currentStep = STEP3_PUMP_CHEM;
                                xSemaphoreGive(stateMutex);
                            }
                            SerialLog::log("✅ STEP2 Complete - Chemical diluted");
                        }
                        break;
                    }

                    case STEP3_PUMP_CHEM:
                    {
                        Rchetopump.on();
                        Rpumptomix.on();
                        Pump.on();
                        
                        float targetVolume = localParams.totalWater + localParams.chemAmount;
                        if (mixLevel >= targetVolume) {
                            closeAllValves();
                            Pump.off();
                            if (xSemaphoreTake(stateMutex, portMAX_DELAY)) {
                                currentStep = STEP4_MIX;
                                xSemaphoreGive(stateMutex);
                            }
                            SerialLog::log("✅ STEP3 Complete - Solution pumped");
                        }
                        break;
                    }

                    case STEP4_MIX:
                        if (!mixing) {
                            Pump.on();
                            mixStartTime = millis();
                            mixing = true;
                            SerialLog::log("🌀 STEP4: Mixing for 10 seconds...");
                        }
                        
                        if (millis() - mixStartTime >= 10000) {  // 10 seconds
                            Pump.off();
                            printProcessSummary(waterLevel, chemLevel, mixLevel);
                            
                            if (xSemaphoreTake(stateMutex, portMAX_DELAY)) {
                                currentMode = MODE_IDLE;
                                currentStep = STEP_IDLE;
                                processParams.isValid = false;
                                xSemaphoreGive(stateMutex);
                            }
                            mixing = false;
                        }
                        break;
                }
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(100));  // 10Hz control loop
    }
}

// ═════════════════════════════════════════════════════════════
// TASK 3: NETWORK MANAGER (Priority 2 - Core 1)
// Handles WiFi/MQTT with lowest priority
// ═════════════════════════════════════════════════════════════
void TaskNetwork(void *pvParameters) {
    SerialLog::log("[NET] Task started on Core", xPortGetCoreID());
    
    // Wait for other tasks to initialize
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    SerialLog::log("🌐 Initializing network...");
    networkInit();
    
    unsigned long lastPublish = 0;
    unsigned long lastMaintain = 0;

    while (true) {
        unsigned long currentMillis = millis();

        // Network maintenance
        if (currentMillis - lastMaintain >= 5000) {
            lastMaintain = currentMillis;
            networkMaintain();
        }

        // Publish sensor data (reads sensors directly)
        if (currentMillis - lastPublish >= 1000) {  // 1 second
            lastPublish = currentMillis;
            
            // DIRECT SENSOR READING (no shared variables)
            float waterLevel = waterTank.getValue();
            float chemLevel = chemTank.getValue();
            float mixLevel = mixTank.getValue();
            
            // Publish to MQTT/Cloud
            networkPublish(waterLevel, chemLevel, mixLevel);
            
            SerialLog::log("[NET] Published sensor data");
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ═════════════════════════════════════════════════════════════
// SETUP & LOOP
// ═════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n╔═════════════════════════════════════════╗");
    Serial.println("║   PUMP CONTROLLER - OPTIMIZED V3.0    ║");
    Serial.println("╚═════════════════════════════════════════╝");
    Serial.flush();

    // Initialize hardware
    SerialLog::log("🔧 Initializing I2C...");
    Wire.begin(SDA_PIN, SCL_PIN, I2C_FREQ);
    delay(100);
    
    SerialLog::log("🔧 Initializing GPIO expander...");
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
    
    SerialLog::log("✅ Hardware initialized");

    // Create single mutex
    stateMutex = xSemaphoreCreateMutex();
    
    // Create optimized tasks
    SerialLog::log("\n🚀 Creating 3 optimized tasks...");
    
    // Task 1: Command Handler (Priority 4, Core 0)
    xTaskCreatePinnedToCore(
        TaskCommandHandler, "CMD", 8192, NULL, 4, &taskCommandHandle, 0);
    SerialLog::log("  ✓ Task 1: Command Handler (Priority 4, Core 0)");
    
    // Task 2: Control & Sensor Combined (Priority 3, Core 0)
    xTaskCreatePinnedToCore(
        TaskControl, "CTRL", 8192, NULL, 3, &taskControlHandle, 0);
    SerialLog::log("  ✓ Task 2: Control & Sensor (Priority 3, Core 0)");
    
    // Task 3: Network Manager (Priority 2, Core 1)
    xTaskCreatePinnedToCore(
        TaskNetwork, "NET", 8192, NULL, 2, &taskNetworkHandle, 1);
    SerialLog::log("  ✓ Task 3: Network Manager (Priority 2, Core 1)");

    SerialLog::log("\n📖 COMMAND REFERENCE:");
    SerialLog::log("   START 500/50/300  - Begin process (Water/Dilute/Chemical)");
    SerialLog::log("   V1                - Clean water irrigation");
    SerialLog::log("   V2                - Solution irrigation");
    SerialLog::log("   STOP              - Emergency stop");
    SerialLog::log("   STATUS            - Auto-displayed every second");
    
    SerialLog::log("\n✅ SYSTEM READY - 3 Tasks, Direct Sensor Reading");
    SerialLog::log("══════════════════════════════════════════\n");
}

void loop() {
    vTaskDelay(portMAX_DELAY);
}