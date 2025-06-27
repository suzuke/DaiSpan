#include "HomeSpan.h"
#include "controller/ThermostatController.h"
#include "controller/MockThermostatController.h"
#include "device/ThermostatDevice.h"
#include "protocol/S21Protocol.h"
#include "protocol/IACProtocol.h"
#include "protocol/ACProtocolFactory.h"
#include "common/Debug.h"
#include "common/Config.h"
#include "common/WiFiManager.h"
#include <ArduinoOTA.h>
#include "common/OTAManager.h"
#include "WiFi.h"
#include "WebServer.h"
#include "ArduinoOTA.h"
#include "common/WebUI.h"
#include "common/WebTemplates.h"


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
std::unique_ptr<ACProtocolFactory> protocolFactory = nullptr;
IThermostatControl* thermostatController = nullptr;
MockThermostatController* mockController = nullptr;  // 模擬控制器專用指針
ThermostatDevice* thermostatDevice = nullptr;
SpanAccessory* accessory = nullptr;
bool deviceInitialized = false;
bool homeKitInitialized = false;

// 配置和管理器
ConfigManager configManager;
WiFiManager* wifiManager = nullptr;
OTAManager* otaManager = nullptr;

// WebServer用於HomeKit模式下的只讀監控
WebServer* webServer = nullptr;
bool monitoringEnabled = false;
bool homeKitPairingActive = false; // 配對期間暫停WebServer

// 安全重啟函數
void safeRestart() {
  DEBUG_INFO_PRINT("[Main] 開始安全重啟...\n");
  delay(500);
  ESP.restart();
}

// 初始化WebServer監控功能（兼容HomeSpan的TCP堆棧）
void initializeMonitoring() {
  if (monitoringEnabled || !homeKitInitialized) {
    return;
  }
  
  DEBUG_INFO_PRINT("[Main] 啟動WebServer監控功能...\n");
  DEBUG_INFO_PRINT("[Main] 可用記憶體: %d bytes\n", ESP.getFreeHeap());
  
  // 檢查記憶體是否足夠 (降低門檻至80KB)
  if (ESP.getFreeHeap() < 80000) {
    DEBUG_ERROR_PRINT("[Main] 記憶體不足(%d bytes)，跳過WebServer啟動\n", ESP.getFreeHeap());
    return;
  }
  
  DEBUG_INFO_PRINT("[Main] 記憶體檢查通過，開始啟動WebServer\n");
  
  webServer = new WebServer(8080); // 使用8080端口避免與HomeSpan衝突
  
  // 首頁 - 系統狀態 (優化版本，快速響應)
  webServer->on("/", [](){
    // 檢查配對狀態，配對期間返回簡化頁面
    if (homeKitPairingActive) {
      String simpleHtml = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
      simpleHtml += "<title>DaiSpan - 配對中</title></head><body>";
      simpleHtml += "<h1>HomeKit 配對進行中</h1>";
      simpleHtml += "<p>設備正在進行HomeKit配對，請稍候...</p>";
      simpleHtml += "<script>setTimeout(function(){location.reload();}, 5000);</script>";
      simpleHtml += "</body></html>";
      webServer->send(200, "text/html", simpleHtml);
      return;
    }
    
    webServer->sendHeader("Cache-Control", "no-cache, must-revalidate");
    webServer->sendHeader("Pragma", "no-cache");
    webServer->sendHeader("Connection", "close"); // 確保連接關閉
    
    // 簡化的HTML生成，減少String操作
    String html = WebUI::getPageHeader("DaiSpan 監控", true, 30);
    html.reserve(800); // 預分配緩衝區
    html += "<div class=\"container\"><h1>DaiSpan 智能恆溫器</h1>";
    html += "<div class=\"status\"><h3>HomeKit 運行模式</h3>";
    html += "<p><strong>WiFi:</strong> " + WiFi.SSID() + "</p>";
    html += "<p><strong>IP:</strong> " + WiFi.localIP().toString() + "</p>";
    html += "<p><strong>記憶體:</strong> " + String(ESP.getFreeHeap()) + "B</p></div>";
    html += "<div style=\"text-align:center;\">";
    html += "<a href=\"/status\" class=\"button\">狀態</a>";
    html += "<a href=\"/wifi\" class=\"button\">WiFi</a>";
    html += "<a href=\"/homekit\" class=\"button\">HomeKit</a>";
    if (configManager.getSimulationMode()) {
      html += "<a href=\"/simulation\" class=\"button\">模擬</a>";
    }
    html += "<a href=\"/simulation-toggle\" class=\"button\">";
    html += configManager.getSimulationMode() ? "真實" : "模擬";
    html += "</a><a href=\"/ota\" class=\"button\">OTA</a></div></div>";
    html += WebUI::getPageFooter();
    
    webServer->send(200, "text/html", html);
  });
  
  // 詳細狀態頁面（簡化版，避免記憶體碎片）
  webServer->on("/status", [](){
    // 使用 WebUI 的現有方法但進行優化
    String html = WebUI::getPageHeader("系統詳細狀態");
    html.reserve(1200); // 預分配緩衝區
    html += "<div class=\"container\"><h1>系統詳細狀態</h1>";
    html += "<div style=\"text-align:center;margin:15px 0;\">";
    html += "<button class=\"button\" onclick=\"location.reload()\">刷新狀態</button>";
    html += "<button class=\"button\" onclick=\"window.open('/status-api','_blank')\">JSON API</button>";
    html += "</div>";
    
    // 使用 WebUI 的系統狀態卡片
    html += WebUI::getSystemStatusCard();
    
    html += "<div style=\"text-align:center;margin:20px 0;\">";
    html += "<a href=\"/\" style=\"color:#007cba;text-decoration:none;\">返回主頁</a>";
    html += "</div></div></body></html>";
    
    webServer->send(200, "text/html", html);
  });
    
  // JSON狀態API (使用預編譯模板 - 唯一成功的記憶體優化)
  webServer->on("/status-api", [](){
    // 使用預編譯模板生成JSON，避免String拼接
    String json = WebTemplates::generateJsonApi(
        WiFi.SSID(),
        WiFi.localIP().toString(),
        WiFi.macAddress(),
        WiFi.RSSI(),
        WiFi.gatewayIP().toString(),
        ESP.getFreeHeap(),
        homeKitInitialized,
        deviceInitialized,
        millis()
    );
    
    webServer->send(200, "application/json", json);
  });
  
  // WiFi配置頁面
  webServer->on("/wifi", [](){
    String html = WebUI::getSimpleWiFiConfigPage();
    webServer->send(200, "text/html", html);
  });
  
  // WiFi掃描API（返回JSON格式）
  webServer->on("/wifi-scan", [](){
    DEBUG_INFO_PRINT("[Main] 開始WiFi掃描...\n");
    int networkCount = WiFi.scanNetworks();
    DEBUG_INFO_PRINT("[Main] 掃描完成，找到 %d 個網路\n", networkCount);
    
    String json = "[";
    int validNetworks = 0;
    
    if (networkCount > 0) {
      for (int i = 0; i < networkCount && i < 15; i++) { // 最多顯示15個網路
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue; // 跳過空SSID
        
        int rssi = WiFi.RSSI(i);
        bool secure = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        
        if (validNetworks > 0) json += ",";
        json += "{";
        json += "\"ssid\":\"" + ssid + "\",";
        json += "\"rssi\":" + String(rssi) + ",";
        json += "\"secure\":" + String(secure ? "true" : "false");
        json += "}";
        validNetworks++;
      }
    }
    
    json += "]";
    
    DEBUG_INFO_PRINT("[Main] 返回 %d 個有效網路\n", validNetworks);
    webServer->send(200, "application/json", json);
  });
  
  // WiFi配置保存處理
  webServer->on("/wifi-save", HTTP_POST, [](){
    String ssid = webServer->arg("ssid");
    String password = webServer->arg("password");
    
    if (ssid.length() > 0) {
      DEBUG_INFO_PRINT("[Main] 保存新WiFi配置: SSID=%s\n", ssid.c_str());
      
      // 保存新的WiFi配置
      configManager.setWiFiCredentials(ssid, password);
      
      String message = "新的WiFi配置已保存成功！<br>";
      message += "設備將重啟並嘗試連接到：<strong>" + ssid + "</strong><br>";
      message += "重啟後請等待設備重新連接，然後訪問新的IP地址。";
      String html = WebUI::getSuccessPage("WiFi配置已保存", message, 3, "/restart");
      webServer->send(200, "text/html", html);
    } else {
      webServer->send(400, "text/plain", "SSID不能為空");
    }
  });
  
  // HomeKit配置頁面
  webServer->on("/homekit", [](){
    // 當前配置資訊
    String currentPairingCode = configManager.getHomeKitPairingCode();
    String currentDeviceName = configManager.getHomeKitDeviceName();
    String currentQRID = configManager.getHomeKitQRID();
    
    String html = WebUI::getHomeKitConfigPage("/homekit-save", currentPairingCode, currentDeviceName, currentQRID, homeKitInitialized);
    webServer->send(200, "text/html", html);
  });
  
  // HomeKit配置保存處理
  webServer->on("/homekit-save", HTTP_POST, [](){
    String pairingCode = webServer->arg("pairing_code");
    String deviceName = webServer->arg("device_name");
    String qrId = webServer->arg("qr_id");
    
    bool configChanged = false;
    String currentPairingCode = configManager.getHomeKitPairingCode();
    String currentDeviceName = configManager.getHomeKitDeviceName();
    String currentQRID = configManager.getHomeKitQRID();
    
    // 檢查配對碼
    if (pairingCode.length() > 0) {
      if (pairingCode.length() == 8 && pairingCode != currentPairingCode) {
        // 驗證是否為8位數字
        bool validCode = true;
        for (int i = 0; i < 8; i++) {
          if (!isDigit(pairingCode.charAt(i))) {
            validCode = false;
            break;
          }
        }
        if (validCode) {
          currentPairingCode = pairingCode;
          configChanged = true;
          DEBUG_INFO_PRINT("[Main] 更新HomeKit配對碼\n");
        } else {
          webServer->send(400, "text/plain", "配對碼必須是8位數字");
          return;
        }
      }
    }
    
    // 檢查設備名稱
    if (deviceName.length() > 0 && deviceName != currentDeviceName) {
      currentDeviceName = deviceName;
      configChanged = true;
      DEBUG_INFO_PRINT("[Main] 更新設備名稱: %s\n", deviceName.c_str());
    }
    
    // 檢查QR ID
    if (qrId.length() > 0 && qrId != currentQRID) {
      currentQRID = qrId;
      configChanged = true;
      DEBUG_INFO_PRINT("[Main] 更新QR ID: %s\n", qrId.c_str());
    }
    
    if (configChanged) {
      // 保存HomeKit配置
      configManager.setHomeKitConfig(currentPairingCode, currentDeviceName, currentQRID);
      
      String message = "<strong>配置更新成功！</strong><br>";
      if (pairingCode.length() > 0) {
        message += "新配對碼：<strong>" + pairingCode + "</strong><br>";
      }
      if (deviceName.length() > 0) {
        message += "新設備名稱：<strong>" + deviceName + "</strong><br>";
      }
      if (qrId.length() > 0) {
        message += "新QR ID：<strong>" + qrId + "</strong><br>";
      }
      message += "<br>⚠️ <strong>重新配對提醒</strong><br>";
      message += "設備將重啟並應用新配置<br>";
      message += "請從家庭App移除舊設備，然後重新添加";
      String html = WebUI::getSuccessPage("HomeKit配置已保存", message, 3, "/restart");
      webServer->send(200, "text/html", html);
    } else {
      String message = "您沒有修改任何配置，或輸入的值與當前配置相同。<br><br>";
      message += "<a href=\"/homekit\" class=\"button\">⬅️ 返回HomeKit配置</a>";
      String html = WebUI::getSuccessPage("無需更新", message, 0);
      webServer->send(200, "text/html", html);
    }
  });
  
  // 模擬控制頁面（僅在模擬模式下可用）
  webServer->on("/simulation", [](){
    DEBUG_INFO_PRINT("[Web] 模擬控制頁面請求 - 模擬模式: %s, mockController: %s\n",
                     configManager.getSimulationMode() ? "啟用" : "停用",
                     mockController ? "有效" : "無效");
    
    if (!configManager.getSimulationMode()) {
      DEBUG_ERROR_PRINT("[Web] 模擬功能未啟用\n");
      webServer->send(403, "text/plain", "模擬功能未啟用");
      return;
    }
    
    if (!mockController) {
      DEBUG_ERROR_PRINT("[Web] 模擬控制器不可用\n");
      webServer->send(500, "text/plain", "模擬控制器不可用");
      return;
    }
    
    String html = WebUI::getSimulationControlPage("/simulation-control",
                                                 mockController->getPower(),
                                                 mockController->getTargetMode(),
                                                 mockController->getTargetTemperature(),
                                                 mockController->getCurrentTemperature(),
                                                 mockController->getSimulatedRoomTemp(),
                                                 mockController->isSimulationHeating(),
                                                 mockController->isSimulationCooling());
    webServer->send(200, "text/html", html);
  });
  
  // 模擬控制處理
  webServer->on("/simulation-control", HTTP_POST, [](){
    DEBUG_INFO_PRINT("[Web] 收到模擬控制請求\n");
    
    if (!configManager.getSimulationMode()) {
      DEBUG_ERROR_PRINT("[Web] 模擬功能未啟用\n");
      webServer->send(403, "text/plain", "模擬功能未啟用");
      return;
    }
    
    if (!mockController) {
      DEBUG_ERROR_PRINT("[Web] 模擬控制器不可用 - mockController指針為空\n");
      webServer->send(500, "text/plain", "模擬控制器不可用");
      return;
    }
    
    DEBUG_INFO_PRINT("[Web] 模擬控制器狀態檢查通過\n");
    
    String powerStr = webServer->arg("power");
    String modeStr = webServer->arg("mode");
    String targetTempStr = webServer->arg("target_temp");
    String currentTempStr = webServer->arg("current_temp");
    String roomTempStr = webServer->arg("room_temp");
    
    DEBUG_INFO_PRINT("[Web] 模擬控制請求 - 電源:%s 模式:%s 目標溫度:%s 當前溫度:%s 環境溫度:%s\n",
                     powerStr.c_str(), modeStr.c_str(), targetTempStr.c_str(), 
                     currentTempStr.c_str(), roomTempStr.c_str());
    
    // 電源控制
    if (powerStr.length() > 0) {
      bool power = (powerStr.toInt() == 1);
      DEBUG_INFO_PRINT("[Web] 收到電源控制請求：%s -> %s\n", 
                       mockController->getPower() ? "開啟" : "關閉",
                       power ? "開啟" : "關閉");
      mockController->setPower(power);
      
      // 重要：當電源關閉時，強制設置模式為關閉狀態
      if (!power) {
        DEBUG_INFO_PRINT("[Web] 電源關閉，強制設置模式為關閉狀態\n");
        mockController->setTargetMode(0); // 0 = 關閉模式
      }
      
      DEBUG_INFO_PRINT("[Web] 電源設置完成，當前狀態：%s，模式：%d\n", 
                       mockController->getPower() ? "開啟" : "關閉",
                       mockController->getTargetMode());
    }
    
    // 模式控制（僅在電源開啟時才處理模式變更）
    if (modeStr.length() > 0 && mockController->getPower()) {
      uint8_t mode = modeStr.toInt();
      if (mode >= 0 && mode <= 3) {
        DEBUG_INFO_PRINT("[Web] 收到模式控制請求：%d -> %d\n", 
                         mockController->getTargetMode(), mode);
        mockController->setTargetMode(mode);
        DEBUG_INFO_PRINT("[Web] 模式設置完成，當前模式：%d\n", 
                         mockController->getTargetMode());
      }
    } else if (modeStr.length() > 0 && !mockController->getPower()) {
      DEBUG_INFO_PRINT("[Web] 電源關閉時忽略模式變更請求\n");
    }
    
    // 目標溫度（僅在電源開啟時才處理）
    if (targetTempStr.length() > 0 && mockController->getPower()) {
      float targetTemp = targetTempStr.toFloat();
      if (targetTemp >= 16.0f && targetTemp <= 30.0f) {
        DEBUG_INFO_PRINT("[Web] 收到目標溫度設置請求：%.1f°C\n", targetTemp);
        mockController->setTargetTemperature(targetTemp);
      }
    } else if (targetTempStr.length() > 0 && !mockController->getPower()) {
      DEBUG_INFO_PRINT("[Web] 電源關閉時忽略溫度設置請求\n");
    }
    
    // 當前溫度
    if (currentTempStr.length() > 0) {
      float currentTemp = currentTempStr.toFloat();
      if (currentTemp >= 10.0f && currentTemp <= 40.0f) {
        mockController->setCurrentTemperature(currentTemp);
      }
    }
    
    // 環境溫度
    if (roomTempStr.length() > 0) {
      float roomTemp = roomTempStr.toFloat();
      if (roomTemp >= 10.0f && roomTemp <= 40.0f) {
        mockController->setSimulatedRoomTemp(roomTemp);
      }
    }
    
    // 建立狀態信息
    String statusInfo = "<h3>📊 當前狀態</h3>";
    statusInfo += "<p><strong>電源：</strong>" + String(mockController->getPower() ? "開啟" : "關閉") + "</p>";
    statusInfo += "<p><strong>模式：</strong>" + String(mockController->getTargetMode());
    switch(mockController->getTargetMode()) {
      case 0: statusInfo += " (關閉)"; break;
      case 1: statusInfo += " (制熱)"; break;
      case 2: statusInfo += " (制冷)"; break;
      case 3: statusInfo += " (自動)"; break;
    }
    statusInfo += "</p>";
    statusInfo += "<p><strong>目標溫度：</strong>" + String(mockController->getTargetTemperature(), 1) + "°C</p>";
    statusInfo += "<p><strong>當前溫度：</strong>" + String(mockController->getCurrentTemperature(), 1) + "°C</p>";
    
    String message = "模擬參數已成功更新！<br><br>" + statusInfo;
    message += "<br><div style=\"margin:20px 0;\">";
    message += "<a href=\"/simulation\" class=\"button\">🔧 返回模擬控制</a>";
    message += "<a href=\"/\" class=\"button\">🏠 返回主頁</a>";
    message += "</div>";
    message += "<p style=\"color:#666;font-size:14px;\">💡 提示：可以在HomeKit app中查看狀態變化</p>";
    
    String html = WebUI::getSuccessPage("設置已更新", message, 0);
    webServer->send(200, "text/html", html);
  });
  
  // 模式切換頁面
  webServer->on("/simulation-toggle", [](){
    String html = WebUI::getSimulationTogglePage("/simulation-toggle-confirm", configManager.getSimulationMode());
    webServer->send(200, "text/html", html);
  });
  
  // 模式切換確認
  webServer->on("/simulation-toggle-confirm", HTTP_POST, [](){
    bool currentMode = configManager.getSimulationMode();
    configManager.setSimulationMode(!currentMode);
    
    String message = "運行模式已切換為：<strong>" + String(!currentMode ? "🔧 模擬模式" : "🏭 真實模式") + "</strong><br>";
    message += "設備將重啟並應用新設定...";
    String html = WebUI::getSuccessPage("模式切換中", message, 3, "/restart");
    webServer->send(200, "text/html", html);
  });
  
  // 重啟端點
  webServer->on("/restart", [](){
    String html = WebUI::getRestartPage(WiFi.localIP().toString() + ":8080");
    webServer->send(200, "text/html", html);
    delay(1000);
    safeRestart();
  });
  
  // 設置WebServer超時和連接限制
  webServer->onNotFound([](){
    webServer->sendHeader("Connection", "close");
    webServer->send(404, "text/plain", "Not Found");
  });
  
  webServer->begin();
  monitoringEnabled = true;
  
  DEBUG_INFO_PRINT("[Main] WebServer監控功能已啟動: http://%s:8080\n", WiFi.localIP().toString().c_str());
  DEBUG_INFO_PRINT("[Main] 注意：HomeKit配對期間WebServer將暫停響應\n");
}

// 初始化HomeKit功能（只有在WiFi穩定連接後調用）
void initializeHomeKit() {
  if (homeKitInitialized) {
    return;
  }
  
  DEBUG_INFO_PRINT("[Main] 開始初始化HomeKit...\n");
  
  // 使用配置管理器中的HomeKit設置
  String pairingCode = configManager.getHomeKitPairingCode();
  String deviceName = configManager.getHomeKitDeviceName();
  String qrId = configManager.getHomeKitQRID();
  
  homeSpan.setPairingCode(pairingCode.c_str());
  homeSpan.setStatusPin(2);
  homeSpan.setHostNameSuffix("");
  homeSpan.setQRID(qrId.c_str());
  homeSpan.setPortNum(1201);        // 改變HomeSpan端口，讓WebServer使用8080
  // 注意：HomeSpan 2.1.2版本不支援setMaxConnections，使用預設TCP連接配置
  // 暫時關閉WebLog以節省記憶體
  // homeSpan.enableWebLog(50,"pool.ntp.org","UTC-8","log");
  
  DEBUG_INFO_PRINT("[Main] HomeKit配置 - 配對碼: %s, 設備名稱: %s\n", 
                   pairingCode.c_str(), deviceName.c_str());

  // 初始化 HomeSpan（記憶體優化配置）
  homeSpan.setLogLevel(1);  // 啟用基本日誌以診斷配對問題
  homeSpan.setControlPin(0);  // 設置控制引腳為Boot按鈕（用於重置配對）
  homeSpan.setStatusPin(2);   // 設置狀態引腳為內建LED
  // 關閉HomeSpan OTA，使用Arduino OTA
  // homeSpan.enableOTA();
  
  DEBUG_INFO_PRINT("[Main] 開始HomeSpan初始化...\n");
  homeSpan.begin(Category::Thermostats, deviceName.c_str());
  DEBUG_INFO_PRINT("[Main] HomeSpan初始化完成\n");
  
  // 立即創建 HomeSpan 配件和服務（記憶體優化）
  accessory = new SpanAccessory();
    new Service::AccessoryInformation();
    new Characteristic::Name("Thermostat");  // 使用短名稱節省記憶體
    new Characteristic::Manufacturer("DaiSpan");
    new Characteristic::SerialNumber("123");  // 縮短序列號
    new Characteristic::Model("TH1.0");  // 縮短型號名稱
    new Characteristic::FirmwareRevision("1.0");
    new Characteristic::Identify();
    
    // 創建ThermostatDevice（在同一作用域內以確保正確註冊到HomeKit）
    if (deviceInitialized && thermostatController) {
      DEBUG_INFO_PRINT("[Main] 硬件已初始化，創建ThermostatDevice\n");
      thermostatDevice = new ThermostatDevice(*thermostatController);
      if (!thermostatDevice) {
        DEBUG_ERROR_PRINT("[Main] 創建 ThermostatDevice 失敗\n");
      } else {
        DEBUG_INFO_PRINT("[Main] ThermostatDevice 創建成功並註冊到HomeKit\n");
      }
    } else {
      DEBUG_ERROR_PRINT("[Main] 硬件未初始化，無法創建ThermostatDevice\n");
      DEBUG_ERROR_PRINT("[Main] deviceInitialized=%s, thermostatController=%s\n",
                        deviceInitialized ? "true" : "false",
                        thermostatController ? "valid" : "null");
    }
  
  homeKitInitialized = true;
  
  // 輸出 HomeSpan 狀態和配對資訊
  DEBUG_INFO_PRINT("[Main] HomeKit配件初始化完成\n");
  DEBUG_INFO_PRINT("[Main] HomeSpan狀態 - 網路狀態: %s\n", WiFi.status() == WL_CONNECTED ? "已連接" : "未連接");
  DEBUG_INFO_PRINT("[Main] HomeSpan狀態 - 設備IP: %s\n", WiFi.localIP().toString().c_str());
  DEBUG_INFO_PRINT("[Main] HomeSpan狀態 - 啟用狀態指示燈和控制按鈕\n");
}

// 初始化硬件組件
void initializeHardware() {
  if (deviceInitialized) {
    DEBUG_INFO_PRINT("[Main] 硬件已經初始化\n");
    return;
  }
  
  // 檢查是否啟用模擬模式
  bool simulationMode = configManager.getSimulationMode();
  
  if (simulationMode) {
    DEBUG_INFO_PRINT("[Main] 啟用模擬模式 - 創建模擬控制器\n");
    
    // 創建模擬控制器
    mockController = new MockThermostatController(25.0f);
    if (!mockController) {
      DEBUG_ERROR_PRINT("[Main] MockThermostatController 創建失敗\n");
      return;
    }
    
    thermostatController = static_cast<IThermostatControl*>(mockController);
    deviceInitialized = true;
    DEBUG_INFO_PRINT("[Main] 模擬模式初始化完成\n");
    
  } else {
    DEBUG_INFO_PRINT("[Main] 啟用真實模式 - 初始化串口通訊...\n");
    
    // 初始化串口通訊
    Serial1.begin(2400, SERIAL_8E2, S21_RX_PIN, S21_TX_PIN);
    delay(200);
    
    // 初始化協議工廠
    protocolFactory = ACProtocolFactory::createFactory();
    if (!protocolFactory) {
      DEBUG_ERROR_PRINT("[Main] 協議工廠創建失敗\n");
      return;
    }
    
    // 創建S21協議實例
    auto protocol = protocolFactory->createProtocol(ACProtocolType::S21_DAIKIN, Serial1);
    if (!protocol) {
      DEBUG_ERROR_PRINT("[Main] S21協議創建失敗\n");
      return;
    }
    
    if (!protocol->begin()) {
      DEBUG_ERROR_PRINT("[Main] 協議初始化失敗\n");
      return;
    }
    delay(200);
    
    thermostatController = new ThermostatController(std::move(protocol));
    if (!thermostatController) {
      DEBUG_ERROR_PRINT("[Main] ThermostatController 創建失敗\n");
      return;
    }
    delay(200);
    
    deviceInitialized = true;
    DEBUG_INFO_PRINT("[Main] 真實硬件初始化完成\n");
  }
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
    // WiFi 功率設定 (可根據性能需求調整)
    #ifdef HIGH_PERFORMANCE_WIFI
      WiFi.setTxPower(WIFI_POWER_19_5dBm);  // 高性能模式
      DEBUG_INFO_PRINT("[Main] WiFi 高性能模式已啟用\n");
    #else
      WiFi.setTxPower(WIFI_POWER_8_5dBm);   // 節能模式 (預設)
      DEBUG_INFO_PRINT("[Main] WiFi 節能模式已啟用\n");
    #endif
    
    // 監控記憶體使用情況
    DEBUG_INFO_PRINT("[Main] 可用堆內存: %d bytes\n", ESP.getFreeHeap());
  #endif

  // 初始化配置管理器
  configManager.begin();
  
  // 初始化WiFi管理器
  wifiManager = new WiFiManager(configManager);
  
  // 檢查WiFi配置狀態
  if (!configManager.isWiFiConfigured()) {
    DEBUG_INFO_PRINT("[Main] 未找到WiFi配置，啟動配置模式\n");
    
    // 啟動AP模式進行WiFi配置（不啟動HomeKit）
    wifiManager->begin();
    
    DEBUG_INFO_PRINT("[Main] 請連接到 DaiSpan-Config 進行WiFi配置\n");
    return; // 在配置模式下，不啟動HomeKit
  }
  
  // 有WiFi配置，嘗試連接並啟動HomeKit
  DEBUG_INFO_PRINT("[Main] 找到WiFi配置，嘗試連接...\n");
  
  String ssid = configManager.getWiFiSSID();
  String password = configManager.getWiFiPassword();
  
  // 連接WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  
  // 等待WiFi連接
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    DEBUG_VERBOSE_PRINT(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    DEBUG_INFO_PRINT("\n[Main] WiFi連接成功: %s\n", WiFi.localIP().toString().c_str());
    
    // 初始化Arduino OTA
    ArduinoOTA.setHostname("DaiSpan-Thermostat");
    ArduinoOTA.setPassword("12345678");  // 設置OTA密碼
    ArduinoOTA.setPort(3232);  // 標準OTA端口
    
    // OTA事件回調
    ArduinoOTA.onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH) {
        type = "sketch";
      } else { // U_SPIFFS
        type = "filesystem";
      }
      DEBUG_INFO_PRINT("[OTA] 開始更新 %s\n", type.c_str());
    });
    
    ArduinoOTA.onEnd([]() {
      DEBUG_INFO_PRINT("[OTA] 更新完成\n");
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      static unsigned int lastPercent = 0;
      unsigned int percent = (progress / (total / 100));
      if (percent != lastPercent && percent % 10 == 0) {
        DEBUG_INFO_PRINT("[OTA] 進度: %u%%\n", percent);
        lastPercent = percent;
      }
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
      DEBUG_ERROR_PRINT("[OTA] 錯誤[%u]: ", error);
      if (error == OTA_AUTH_ERROR) DEBUG_ERROR_PRINT("認證失敗\n");
      else if (error == OTA_BEGIN_ERROR) DEBUG_ERROR_PRINT("開始失敗\n");
      else if (error == OTA_CONNECT_ERROR) DEBUG_ERROR_PRINT("連接失敗\n");
      else if (error == OTA_RECEIVE_ERROR) DEBUG_ERROR_PRINT("接收失敗\n");
      else if (error == OTA_END_ERROR) DEBUG_ERROR_PRINT("結束失敗\n");
    });
    
    ArduinoOTA.begin();
    DEBUG_INFO_PRINT("[Main] Arduino OTA已啟用 - 主機名: DaiSpan-Thermostat\n");
    
    // WiFi連接成功，不啟動Web服務器，專注於HomeKit功能
    
    // 先初始化硬件組件
    initializeHardware();
    
    // 然後初始化HomeKit（硬件準備好後）
    initializeHomeKit();
    
    // 先停止 AP 模式（如果正在運行），防止干擾HomeKit
    if (wifiManager && wifiManager->isInAPMode()) {
      wifiManager->stopAPMode();
      delay(500); // 等待 AP 模式完全停止
    }
    
    // 清理WiFi管理器，避免干擾HomeKit
    delete wifiManager;
    wifiManager = nullptr;
    
    // 初始化簡單的OTA功能
    ArduinoOTA.setHostname("DaiSpan-Thermostat");
    ArduinoOTA.begin();
    
    // WebServer監控將在loop()中非阻塞啟動
    DEBUG_INFO_PRINT("[Main] HomeKit模式啟動，WebServer監控將延遲啟動\n");
  } else {
    DEBUG_ERROR_PRINT("\n[Main] WiFi連接失敗，啟動配置模式\n");
    
    // WiFi連接失敗，啟動AP模式
    wifiManager->begin();
  }
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

  // 處理Arduino OTA（高優先級，頻繁調用）
  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();
  }
  
  // 根據模式處理不同的邏輯
  if (homeKitInitialized) {
    // HomeKit模式：處理HomeSpan、WebServer和OTA
    // 優化處理順序，避免資源競爭
    
    // 檢查是否需要延遲啟動WebServer（針對setup()路徑）
    static unsigned long homeKitReadyTime = 0;
    static bool webServerStartScheduled = false;
    static bool homeKitStabilized = false;
    
    if (!webServerStartScheduled && !monitoringEnabled) {
      homeKitReadyTime = millis();
      webServerStartScheduled = true;
      DEBUG_INFO_PRINT("[Main] WebServer將在HomeKit穩定後啟動（延遲5秒）\n");
    }
    
    // 等待5秒讓HomeKit完全穩定，然後再啟動WebServer
    if (webServerStartScheduled && !monitoringEnabled && 
        millis() - homeKitReadyTime >= 5000 && !homeKitPairingActive) {
      DEBUG_INFO_PRINT("[Main] HomeKit已穩定，開始啟動WebServer（setup路徑）\n");
      initializeMonitoring();
      homeKitStabilized = true;
    }
    
    // 優先處理 HomeKit (最重要)
    homeSpan.poll();
    
    // 檢測 HomeKit 配對狀態 (更準確的檢測方法)
    static unsigned long lastPairingCheck = 0;
    static bool wasPairing = false;
    
    if (currentTime - lastPairingCheck >= 2000) { // 每2秒檢查一次
      // 檢查HomeSpan的配對狀態
      // 當有新配對請求時，TCP連接數會增加
      static uint32_t baseMemory = ESP.getFreeHeap();
      uint32_t currentMemory = ESP.getFreeHeap();
      
      // 配對活動檢測：記憶體使用變化 + TCP活動
      bool memoryActivity = (baseMemory > currentMemory + 15000) || (currentMemory > baseMemory + 15000);
      homeKitPairingActive = memoryActivity;
      
      // 記錄配對狀態變化
      if (homeKitPairingActive != wasPairing) {
        DEBUG_INFO_PRINT("[Main] HomeKit配對狀態變化: %s\n", 
                         homeKitPairingActive ? "配對中" : "空閒");
        if (homeKitPairingActive) {
          DEBUG_INFO_PRINT("[Main] 配對期間暫停WebServer處理以確保穩定性\n");
        } else {
          DEBUG_INFO_PRINT("[Main] 配對完成，恢復WebServer處理\n");
        }
        wasPairing = homeKitPairingActive;
      }
      
      baseMemory = currentMemory; // 更新基準值
      lastPairingCheck = currentTime;
    }
    
    // 配對期間完全暫停WebServer，確保HomeKit穩定
    if (!homeKitPairingActive && monitoringEnabled && webServer) {
      static unsigned long lastWebServerHandle = 0;
      // 正常情況下限制WebServer頻率
      if (currentTime - lastWebServerHandle >= 50) {
        webServer->handleClient();
        lastWebServerHandle = currentTime;
      }
    }
    
    // OTA 處理（低優先級）
    static unsigned long lastOTAHandle = 0;
    if (currentTime - lastOTAHandle >= 100) { // 限制每100ms處理一次
      ArduinoOTA.handle();
      lastOTAHandle = currentTime;
    }
  } else if (wifiManager) {
    // 檢查是否WiFi已連接但HomeKit未初始化（需要啟動HomeKit）
    if (WiFi.status() == WL_CONNECTED && !homeKitInitialized && !deviceInitialized) {
      DEBUG_INFO_PRINT("[Main] WiFi已連接，開始初始化HomeKit...\n");
      
      // 先停止 AP 模式，防止模式衝突
      if (wifiManager->isInAPMode()) {
        wifiManager->stopAPMode();
        delay(500); // 等待 AP 模式完全停止
      }
      
      // 清理WiFi管理器
      delete wifiManager;
      wifiManager = nullptr;
      
      // 初始化硬件組件
      initializeHardware();
      
      // 初始化HomeKit
      initializeHomeKit();
      
      // 延遲啟動監控以避免阻塞 (使用非阻塞方式)
      static unsigned long homeKitInitTime = 0;
      static bool monitoringScheduled = false;
      
      if (!monitoringScheduled) {
        homeKitInitTime = millis();
        monitoringScheduled = true;
        DEBUG_INFO_PRINT("[Main] WebServer監控將在HomeKit穩定後啟動（延遲5秒）\n");
      }
      
      // 5秒後非阻塞啟動監控，確保HomeKit完全穩定
      if (monitoringScheduled && millis() - homeKitInitTime >= 5000 && !monitoringEnabled) {
        DEBUG_INFO_PRINT("[Main] HomeKit已穩定，開始啟動WebServer監控\n");
        initializeMonitoring();
      }
      
      DEBUG_INFO_PRINT("[Main] HomeKit初始化完成\n");
    } else {
      // 配置模式：處理WiFi管理器
      wifiManager->loop();
    }
  }

  // 每5秒輸出一次心跳信息
  if (currentTime - lastLoopTime >= HEARTBEAT_INTERVAL) {
    String mode = "";
    if (homeKitInitialized) {
      mode = "HomeKit模式";
    } else if (wifiManager && wifiManager->isInAPMode()) {
      mode = "WiFi配置模式";
    } else {
      mode = "初始化中";
    }
    
    DEBUG_INFO_PRINT("[Main] 主循環運行中... 模式：%s，WiFi：%s，設備：%s，IP：%s\n", 
                     mode.c_str(),
                     WiFi.status() == WL_CONNECTED ? "已連接" : "未連接",
                     deviceInitialized ? "已初始化" : "未初始化",
                     WiFi.localIP().toString().c_str());
    
    // 如果是HomeKit模式，顯示詳細狀態
    if (homeKitInitialized) {
      // 記憶體監控和分析
      static uint32_t minMemory = ESP.getFreeHeap();
      static uint32_t maxMemory = ESP.getFreeHeap();
      uint32_t currentMemory = ESP.getFreeHeap();
      
      if (currentMemory < minMemory) minMemory = currentMemory;
      if (currentMemory > maxMemory) maxMemory = currentMemory;
      
      DEBUG_INFO_PRINT("[Main] HomeKit狀態 - WiFi: %d dBm, 記憶體: %d bytes (最小:%d, 最大:%d), WebServer: %s, 配對中: %s\n", 
                       WiFi.RSSI(), 
                       currentMemory,
                       minMemory,
                       maxMemory,
                       monitoringEnabled ? "啟用" : "停用",
                       homeKitPairingActive ? "是" : "否");
      
      // 記憶體警告
      if (currentMemory < 80000) {
        DEBUG_ERROR_PRINT("[Main] ⚠️ 記憶體不足警告: %d bytes\n", currentMemory);
      }
    }
    lastLoopTime = currentTime;
  }
}