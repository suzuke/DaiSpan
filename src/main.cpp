#include "HomeSpan.h"
#include "controller/ThermostatController.h"
#include "device/ThermostatDevice.h"
#include "protocol/S21Protocol.h"
#include "common/Debug.h"
#include "WiFi.h"

// 使用ESP32的第二個串口（GPIO16=RX2, GPIO17=TX2）
#define S21_RX_PIN 16
#define S21_TX_PIN 17

// 全局變量
S21Protocol* s21Protocol = nullptr;
ThermostatController* thermostatController = nullptr;
ThermostatDevice* thermostatDevice = nullptr;
SpanAccessory* accessory = nullptr;
bool deviceInitialized = false;

// 初始化設備
void initializeDevice() {
  if (deviceInitialized) {
    DEBUG_INFO_PRINT("[Main] 設備已經初始化\n");
    return;
  }
  
  DEBUG_INFO_PRINT("[Main] 開始初始化串口通訊...\n");
  
  // 配置S21通訊用的串口
  Serial2.begin(2400, SERIAL_8E2, S21_RX_PIN, S21_TX_PIN);
  delay(100);  // 等待串口穩定
  DEBUG_INFO_PRINT("[Main] Serial2 初始化完成\n");
  
  // 創建S21協議和恆溫器控制器
  s21Protocol = new S21Protocol(Serial2);
  delay(100);  // 等待協議初始化
  
  // 初始化S21協議
  if (!s21Protocol->begin()) {
    DEBUG_ERROR_PRINT("[Main] S21Protocol 初始化失敗\n");
    return;  // 如果初始化失敗，不繼續後續步驟
  }
  DEBUG_INFO_PRINT("[Main] S21Protocol 初始化完成\n");
  
  thermostatController = new ThermostatController(*s21Protocol);
  delay(100);  // 等待控制器初始化
  DEBUG_INFO_PRINT("[Main] ThermostatController 初始化完成\n");
  
  // 創建 HomeSpan 配件
  accessory = new SpanAccessory();
    new Service::AccessoryInformation();
      new Characteristic::Name("智能恆溫器");
      new Characteristic::Manufacturer("DaiSpan");
      new Characteristic::SerialNumber("123-ABC");
      new Characteristic::Model("恆溫器 v1.0");
      new Characteristic::FirmwareRevision("1.0");
      new Characteristic::Identify();
    
    thermostatDevice = new ThermostatDevice(*thermostatController);
  
  DEBUG_INFO_PRINT("[Main] HomeSpan 配件初始化完成\n");
  deviceInitialized = true;
  DEBUG_INFO_PRINT("[Main] 設備初始化完成\n");
}

// WiFi 連接狀態回調函數
void wifiCallback() {
  DEBUG_INFO_PRINT("[Main] WiFi 連接狀態回調函數\n WiFi.status() = %d\n", WiFi.status());
  if (WiFi.status() == WL_CONNECTED) {
    DEBUG_INFO_PRINT("[Main] WiFi 已連接，SSID：%s，IP：%s\n", 
                     WiFi.SSID().c_str(),
                     WiFi.localIP().toString().c_str());
  } else {
    DEBUG_INFO_PRINT("[Main] WiFi 連接已斷開\n");
  }
}

void setup() {
  Serial.begin(115200);  // 調試用串口
  DEBUG_INFO_PRINT("\n[Main] 開始啟動...\n");
  
  // 先初始化 HomeSpan
  homeSpan.setWifiCredentials("your_ssid", "your_password");
  homeSpan.setWifiCallback(wifiCallback);
  homeSpan.setPairingCode("11122333");
  homeSpan.setStatusPin(2);  // 使用 LED 指示狀態
  homeSpan.setHostNameSuffix("");  // 使用 MAC 地址作為後綴
  homeSpan.setQRID("HSPN");  // 設置 Setup ID
  homeSpan.enableWebLog(500,"pool.ntp.org","UTC-8","log");  // 啟用 web log，保存最近500條記錄

  homeSpan.begin(Category::Thermostats, "DaiSpan Thermostat");
  
  DEBUG_INFO_PRINT("[Main] HomeSpan 初始化完成\n");
  initializeDevice();
}

void loop() {
  static unsigned long lastLoopTime = 0;
  unsigned long currentTime = millis();
  
  // 每5秒輸出一次心跳信息
  if (currentTime - lastLoopTime >= HEARTBEAT_INTERVAL) {
    DEBUG_INFO_PRINT("[Main] 主循環運行中... WiFi：%s，設備：%s，IP：%s\n", 
                     WiFi.status() == WL_CONNECTED ? "已連接" : "未連接",
                     deviceInitialized ? "已初始化" : "未初始化",
                     WiFi.localIP().toString().c_str());
    lastLoopTime = currentTime;
  }
  
  // 只有在設備初始化後才執行更新
  if (deviceInitialized) {
    homeSpan.poll();
  }
}