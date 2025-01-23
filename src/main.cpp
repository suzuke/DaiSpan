#include "HomeSpan.h"
#include "controller/ThermostatController.h"
#include "device/ThermostatDevice.h"
#include "protocol/S21Protocol.h"
#include "common/Debug.h"
#include "WiFi.h"


//根據platformio.ini的env選擇進行define
#if defined(ESP32C3_SUPER_MINI)
#define S21_RX_PIN 4
#define S21_TX_PIN 3
#elif defined(ESP32S3_SUPER_MINI)
#define S21_RX_PIN 13
#define S21_TX_PIN 12
#else
#define S21_RX_PIN 14
#define S21_TX_PIN 13
#endif

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
  
  // 分步驟初始化，每步都加入延遲和檢查
  Serial1.begin(2400, SERIAL_8E2, S21_RX_PIN, S21_TX_PIN);
  delay(200);
  
  s21Protocol = new S21Protocol(Serial1);
  if (!s21Protocol) {
    DEBUG_ERROR_PRINT("[Main] S21Protocol 創建失敗\n");
    return;
  }
  delay(200);
  
  if (!s21Protocol->begin()) {
    DEBUG_ERROR_PRINT("[Main] S21Protocol 初始化失敗\n");
    return;
  }
  delay(200);
  
  thermostatController = new ThermostatController(*s21Protocol);
  if (!thermostatController) {
    DEBUG_ERROR_PRINT("[Main] ThermostatController 創建失敗\n");
    return;
  }
  delay(200);
  
  // 創建 HomeSpan 配件
  accessory = new SpanAccessory();
  if (!accessory) {
    DEBUG_ERROR_PRINT("[Main] 創建 SpanAccessory 失敗\n");
    return;
  }

  // 創建配件信息服務
  new Service::AccessoryInformation();
  new Characteristic::Name("智能恆溫器");
  new Characteristic::Manufacturer("DaiSpan");
  new Characteristic::SerialNumber("123-ABC");
  new Characteristic::Model("恆溫器 v1.0");
  new Characteristic::FirmwareRevision("1.0");
  new Characteristic::Identify();

  // 創建恆溫器設備
  thermostatDevice = new ThermostatDevice(*thermostatController);
  if (!thermostatDevice) {
    DEBUG_ERROR_PRINT("[Main] 創建 ThermostatDevice 失敗\n");
    return;
  }
  
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
  
  #if defined(ESP32C3_SUPER_MINI)
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    // 監控記憶體使用情況
    DEBUG_INFO_PRINT("[Main] 可用堆內存: %d bytes\n", ESP.getFreeHeap());
  #endif

  // 延遲初始化
  delay(1000);  // 給系統一些穩定時間
  
  homeSpan.setWifiCredentials("suzuke", "0978362789");
  homeSpan.setWifiCallback(wifiCallback);
  homeSpan.setPairingCode("11122333");
  homeSpan.setStatusPin(2);
  homeSpan.setHostNameSuffix("");
  homeSpan.setQRID("HSPN");
  homeSpan.enableWebLog(50,"pool.ntp.org","UTC-8","log");  // 進一步減少日誌條目數量

  // 初始化 HomeSpan
  homeSpan.begin(Category::Thermostats, "DaiSpan Thermostat");
  
  DEBUG_INFO_PRINT("[Main] HomeSpan 初始化完成\n");
  
  // 延遲設備初始化並監控記憶體
  delay(1000);
  DEBUG_INFO_PRINT("[Main] 初始化前可用堆內存: %d bytes\n", ESP.getFreeHeap());
  initializeDevice();
  DEBUG_INFO_PRINT("[Main] 初始化後可用堆內存: %d bytes\n", ESP.getFreeHeap());
}

void loop() {
  static unsigned long lastLoopTime = 0;
  unsigned long currentTime = millis();
  
  #if defined(ESP32C3_SUPER_MINI)
    if (WiFi.status() == WL_DISCONNECTED &&
    WiFi.getTxPower() != WIFI_POWER_8_5dBm) {
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
  }
  #endif

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