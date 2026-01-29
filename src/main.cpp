#include <Arduino.h>
#include "Tank.h"
#include "Relay.h"
#include "CommandHandler.h"
#include "network.h"

Tank waterTank(4);
Tank chemTank(5);
Tank mixTank(6);

// ===== KHAI BÁO RELAY =====
Relay Rwatermain(7);
Relay Rmixmain(8);
Relay Rwatertomix(9);
Relay Rmixtoche(10);
Relay Rchetopump(11);
Relay Rpumptomix(12);
Relay Pump(13);

CommandHandler cmdHandler;
enum Step
{
  IDLE,
  STEP1,
  STEP2,
  STEP3,
  STEP4,
};

Step currentStep = IDLE;

float totalWater = 0;  // tổng nước cần cho Mix
float diluteWater = 0; // nước pha hóa chất
float chemAmount = 0;  // lượng hóa chất
float mixStartVol = 0; // mốc thể tích ban đầu

// ===== HELPER FUNCTION =====
void closeAllValves()
{
  Rwatermain.off();
  Rmixmain.off();
  Rwatertomix.off();
  Rmixtoche.off();
  Rchetopump.off();
  Rpumptomix.off();
}

float totalWater = 0;
float diluteWater = 0;
float powderAmount = 0;
float mixStartVol = 0;

// ...existing code...
void handleCommand(const String &cmd)
{
  String c = cmd;
  c.trim();
  c.toUpperCase();

  if (c.startsWith("START "))
  {
    float a, b, d;
    if (sscanf(c.c_str(), "START %f/%f/%f", &a, &b, &d) == 3)
    {
      if (b >= a || a <= 0 || b <= 0 || d <= 0)
      {
        Serial.println("❌ Tham số không hợp lệ");
        return;
      }

      totalWater = a;
      diluteWater = b;
      chemAmount = d;

      mixStartVol = mixTank.getValue();
      currentStep = STEP1;

      Serial.printf("▶ START | Nước: %.0fL | Pha: %.0fL | Hóa chất: %.0fL\n",
                    totalWater, diluteWater, chemAmount);
    }
    else
    {
      Serial.println("❌ Format: START <total>/<dilute>/<chem>");
    }
  }
  else if (c == "V1")
  {
    closeAllValves();
    Rwatermain.on();
    Serial.println("→ Tưới nước sạch");
  }
  else if (c == "V2")
  {
    closeAllValves();
    Rmixmain.on();
    Serial.println("→ Tưới dung dịch");
  }
  else if (c == "STOP")
  {
    closeAllValves();
    Pump.off();
    currentStep = IDLE;
    Serial.println("⊗ STOP");
  }
  else if (c == "STATUS")
  {
    Serial.printf("Water %.1fL | Chem %.1fL | Mix %.1fL\n",
                  waterTank.getValue(),
                  chemTank.getValue(),
                  mixTank.getValue());
  }
}

// ...existing code...

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== PUMP CONTROLLER ===\n");
  Serial.println("Lệnh:");
  Serial.println("  START <lít>   - Bơm nước vào Mix (VD: START 400)");
  Serial.println("  CHEM <lít>    - Lấy hóa chất (VD: CHEM 50)");
  Serial.println("  V1            - Tưới nước sạch");
  Serial.println("  V2            - Tưới dung dịch");
  Serial.println("  STOP          - Dừng tưới\n");

  // Set callback cho CommandHandler
  cmdHandler.begin(handleCommand);

  networkInit();
  delay(500);
}

void loop()
{
  networkMaintain();
  float waterVol = waterTank.getValue();
  float chemVol = chemTank.getValue();
  float mixVol = mixTank.getValue();

  // ===== HANDLE COMMAND TỪ SERIAL, MQTT, BLE =====
  cmdHandler.onSerial();      // Serial dùng chung handler
  cmdHandler.onMQTT(mqttCmd); // MQTT dùng chung handler
  mqttCmd = "";               // Clear để tránh xử lý lặp
  cmdHandler.onBLE(bleCmd);   // BLE dùng chung handler
  bleCmd = "";                // Clear để tránh xử lý lặp

  // ===== STATE MACHINE =====
  switch (currentStep)
  {
  case IDLE:
  {
    closeAllValves();
    break;
  }

  case STEP1:
  {
    Serial.printf("[STEP1] bơm nước vào Mix ");
    Rwatertomix.on();
    Rwatermain.off();
    Rmixtoche.off();
    Rchetopump.off();
    Rpumptomix.off();
    Rmixmain.off();
    if (mixTank.getValue() >= totalWater)
    {
      closeAllValves();
      Serial.printf("\n✓ Đã bơm đủ nước vào Mix: %.0fL\n\n", totalWater);
      Serial.printf("Sẵn sàng lấy hóa chất: CHEM <lít>\n");
      currentStep = STEP2;
    }
    else
    {
      Serial.printf("| Hiện tại: %.1fL / %.0fL\r", mixVol, totalWater);
    }
    break;
  }

  case STEP2:
  {
    // Pha loãng hóa chất: bơm nước từ Mix sang Chem
    // Theo dõi lượng nước đã bơm = mixBeforeChem - mixVol hiện tại
    float waterPumped = totalWater - mixVol;

    Rmixtoche.on(); // Mở van Mix → Chem
    Rwatertomix.off();
    Rwatermain.off();
    Rchetopump.off();
    Rpumptomix.off();
    Rmixmain.off();
    Pump.off();

    Serial.printf("[STEP2] Pha loãng hóa chất | Đã bơm: %.1fL / %.0fL\r", waterPumped, chemAmount);

    // Kiểm tra đã bơm đủ nước để pha loãng chưa
    if (waterPumped >= chemAmount)
    {
      closeAllValves();
      Pump.off();
      Serial.printf("\n✓ Đã pha loãng xong: %.0fL nước vào bình hóa chất\n", chemAmount);
      Serial.println(">>> STEP 3: Bơm hóa chất vào Mix...\n");
      currentStep = STEP3;
    }
    break;
  }

  case STEP3:
  {
    // Bơm hóa chất đã pha loãng từ Chem vào Mix
    // Mở van Chem → Pump → Mix
    Rchetopump.on(); // Van từ Chem đến Pump
    Rpumptomix.on(); // Van từ Pump đến Mix
    Pump.on();       // Bật bơm
    Rwatertomix.off();
    Rwatermain.off();
    Rmixtoche.off();
    Rmixmain.off();

    Serial.printf("[STEP3] Bơm hóa chất vào Mix | Chem: %.1fL | Mix: %.1fL\r", chemVol, mixVol);

    // Kiểm tra bình Chem đã rỗng chưa (hoặc gần rỗng)
    if (chemVol <= 1.0f) // Còn < 1L coi như rỗng
    {
      closeAllValves();
      Pump.off();
      Serial.printf("\n✓ Đã bơm hết hóa chất vào Mix!\n");
      Serial.println(">>> STEP 4: Khuấy trộn...\n");
      currentStep = STEP4;
    }
    break;
  }
  case STEP4:
  {
    closeAllValves();
    Pump.on();

    float finalMixVol = mixTank.getValue();
    float finalMixPercent = mixTank.getPercentage();

    Serial.println("\n========== KẾT QUẢ PHA TRỘN ==========");
    Serial.printf("📦 Mix Tank: %.1fL (%.0f%%)\n", finalMixVol, finalMixPercent);
    Serial.printf("💧 Nước đã dùng: %.1fL\n", totalWater);
    Serial.printf("🧪 Hóa chất đã pha: %.1fL\n", chemAmount);
    Serial.printf("📊 Tỷ lệ pha: 1:%.0f (hóa chất:nước)\n", (totalWater - chemAmount) / chemAmount);
    Serial.println("=======================================\n");
    Serial.println("✓ Hoàn tất quy trình!");
    Serial.println("Sẵn sàng tưới: V1 (nước sạch) | V2 (dung dịch)\n");

    currentStep = IDLE;
    break;
  }
  }

  // ===== MQTT PUBLISH =====
  if (millis() - lastPublish >= 5000)
  {
    lastPublish = millis();
    networkPublish(waterTank.getPercentage(),
                   chemTank.getPercentage(),
                   mixTank.getPercentage());
  }

  delay(100);
}