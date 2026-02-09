/*
  Test biến trở chân 6 - ESP32-S3
  Đọc giá trị analog và hiển thị
*/
#include <Arduino.h>

#define POTENTIOMETER_PIN 6
#define SAMPLE_COUNT 10

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== POTENTIOMETER TEST - PIN 6 ===");
  Serial.println("Reading analog values every 500ms");
  Serial.println("Range: 0-4095 (12-bit ADC)");
  Serial.println("Voltage: 0-3.3V");
  Serial.println("================================\n");
}

void loop() {
  // Đọc giá trị analog
  int rawValue = analogRead(POTENTIOMETER_PIN);
  
  // Tính điện áp (ESP32-S3 có ADC 12-bit, 0-4095)
  float voltage = (rawValue * 3.3) / 4095.0;
  
  // Tính phần trăm (0-100%)
  float percentage = (rawValue * 100.0) / 4095.0;
  
  // Làm mượt bằng cách đọc nhiều lần
  long sum = 0;
  for(int i = 0; i < SAMPLE_COUNT; i++) {
    sum += analogRead(POTENTIOMETER_PIN);
    delay(10);
  }
  int smoothValue = sum / SAMPLE_COUNT;
  float smoothVoltage = (smoothValue * 3.3) / 4095.0;
  float smoothPercentage = (smoothValue * 100.0) / 4095.0;
  
  // In kết quả
  Serial.printf("Raw: %4d | Voltage: %0.2fV | Percent: %5.1f%% | ", 
                rawValue, voltage, percentage);
  Serial.printf("Smooth: %4d | %0.2fV | %5.1f%%\n", 
                smoothValue, smoothVoltage, smoothPercentage);
  
  delay(500);
}