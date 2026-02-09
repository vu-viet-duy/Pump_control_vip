#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n🔍 ESP32-S3 WiFi Scanner & Debugger");
  Serial.println("=====================================");
  
  // Hiển thị thông tin ESP32
  Serial.printf("📱 Chip: %s\n", ESP.getChipModel());
  Serial.printf("🔧 SDK: %s\n", ESP.getSdkVersion());
  Serial.printf("💾 Free Heap: %d KB\n", ESP.getFreeHeap() / 1024);
  
  // Khởi tạo WiFi
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  
  Serial.println("\n📡 Scanning WiFi networks...");
  
  int networks = WiFi.scanNetworks();
  
  if (networks == 0) {
    Serial.println("❌ No networks found!");
  } else {
    Serial.printf("✅ Found %d networks:\n", networks);
    Serial.println("   SSID                      | RSSI | Channel | Security");
    Serial.println("   ========================= | ==== | ======= | ========");
    
    for (int i = 0; i < networks; ++i) {
      String ssid = WiFi.SSID(i);
      int32_t rssi = WiFi.RSSI(i);
      wifi_auth_mode_t auth = WiFi.encryptionType(i);
      int32_t channel = WiFi.channel(i);
      
      // Format SSID (limit to 25 chars)
      String displaySSID = ssid;
      if (displaySSID.length() > 25) {
        displaySSID = displaySSID.substring(0, 22) + "...";
      }
      
      // Security type
      String secType = "";
      switch(auth) {
        case WIFI_AUTH_OPEN: secType = "Open"; break;
        case WIFI_AUTH_WEP: secType = "WEP"; break;
        case WIFI_AUTH_WPA_PSK: secType = "WPA"; break;
        case WIFI_AUTH_WPA2_PSK: secType = "WPA2"; break;
        case WIFI_AUTH_WPA_WPA2_PSK: secType = "WPA/2"; break;
        case WIFI_AUTH_WPA2_ENTERPRISE: secType = "WPA2-E"; break;
        case WIFI_AUTH_WPA3_PSK: secType = "WPA3"; break;
        default: secType = "Unknown"; break;
      }
      
      // Signal quality
      String signalQuality = "";
      if (rssi > -50) signalQuality = "Excellent";
      else if (rssi > -60) signalQuality = "Good";
      else if (rssi > -70) signalQuality = "Fair";
      else if (rssi > -80) signalQuality = "Poor";
      else signalQuality = "Very Poor";
      
      Serial.printf("   %-25s | %4d | %7d | %-8s (%s)\n", 
                    displaySSID.c_str(), rssi, channel, secType.c_str(), signalQuality.c_str());
      
      // Highlight target network
      if (ssid == "Duong Uyen" || ssid == "Duong Uyen 2") {
        Serial.printf("   🎯 TARGET NETWORK FOUND! Signal: %s\n", signalQuality.c_str());
      }
    }
  }
  
  // Test connection với từng SSID có thể
  Serial.println("\n🔌 Testing connections...");
  
  String testSSIDs[] = {"Duong Uyen", "Duong Uyen 2", "DuongUyen", "DuongUyen2"};
  String password = "vuduy2000";
  
  for (int i = 0; i < 4; i++) {
    Serial.printf("\n📡 Trying: '%s'\n", testSSIDs[i].c_str());
    
    WiFi.begin(testSSIDs[i].c_str(), password.c_str());
    
    unsigned long startTime = millis();
    int attempts = 0;
    
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
      delay(500);
      attempts++;
      
      // Print status codes
      switch(WiFi.status()) {
        case WL_IDLE_STATUS:
          Serial.print("⏳ IDLE ");
          break;
        case WL_NO_SSID_AVAIL:
          Serial.print("❌ NO_SSID ");
          break;
        case WL_SCAN_COMPLETED:
          Serial.print("🔍 SCAN_DONE ");
          break;
        case WL_CONNECTED:
          Serial.print("✅ CONNECTED ");
          break;
        case WL_CONNECT_FAILED:
          Serial.print("❌ FAILED ");
          break;
        case WL_CONNECTION_LOST:
          Serial.print("📡 LOST ");
          break;
        case WL_DISCONNECTED:
          Serial.print("🔌 DISCONNECTED ");
          break;
        default:
          Serial.printf("? UNKNOWN(%d) ", WiFi.status());
          break;
      }
      
      if (attempts % 10 == 0) Serial.println();
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n✅ SUCCESS!");
      Serial.printf("   IP: %s\n", WiFi.localIP().toString().c_str());
      Serial.printf("   Signal: %d dBm\n", WiFi.RSSI());
      Serial.printf("   MAC: %s\n", WiFi.macAddress().c_str());
      
      // Test internet connectivity
      Serial.println("🌐 Testing internet...");
      if (WiFi.ping("8.8.8.8") >= 0) {
        Serial.println("✅ Internet OK");
      } else {
        Serial.println("❌ No internet");
      }
      
      WiFi.disconnect();
      delay(2000);
      break;
    } else {
      Serial.println("\n❌ Failed to connect");
      Serial.printf("   Final status: %d\n", WiFi.status());
      WiFi.disconnect();
      delay(2000);
    }
  }
  
  Serial.println("\n🔍 WiFi Debug Complete!");
  Serial.println("Copy results and check:");
  Serial.println("1. Is target network visible?");
  Serial.println("2. Signal strength adequate?");
  Serial.println("3. Security type compatible?");
  Serial.println("4. Any connection successful?");
}

void loop() {
  // Rescan every 30 seconds
  delay(30000);
  Serial.println("\n♻️ Rescanning...");
  setup();
}