#include "HomeSpan.h"
#include "controller/ThermostatController.h"
#include "device/ThermostatDevice.h"
#include "protocol/S21Protocol.h"
#include "common/Debug.h"
#include "common/Config.h"
#include "common/WiFiManager.h"
#include "common/OTAManager.h"
#include "WiFi.h"
#include "WebServer.h"
#include "ArduinoOTA.h"


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
bool homeKitInitialized = false;

// 配置和管理器
ConfigManager configManager;
WiFiManager* wifiManager = nullptr;
OTAManager* otaManager = nullptr;

// WebServer用於HomeKit模式下的只讀監控
WebServer* webServer = nullptr;
bool monitoringEnabled = false;

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
  
  webServer = new WebServer(8080); // 使用8080端口避免與HomeSpan衝突
  
  // 首頁 - 系統狀態  
  webServer->on("/", [](){
    String html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">";
    html += "<title>DaiSpan 監控</title>";
    html += "<style>body{font-family:Arial,sans-serif;margin:20px;background:#f0f0f0;}";
    html += ".container{max-width:600px;margin:0 auto;background:white;padding:20px;border-radius:10px;}";
    html += ".status{background:#e8f4f8;padding:15px;border-radius:5px;margin:10px 0;}";
    html += ".button{display:inline-block;padding:10px 20px;margin:10px 5px;background:#007cba;color:white;text-decoration:none;border-radius:5px;}";
    html += "</style><meta http-equiv=\"refresh\" content=\"30\"></head><body>";
    html += "<div class=\"container\"><h1>🌡️ DaiSpan 智能恆溫器</h1>";
    html += "<div class=\"status\"><h3>系統狀態</h3>";
    html += "<p>模式: HomeKit運行模式</p>";
    html += "<p>WiFi: " + WiFi.SSID() + "</p>";
    html += "<p>IP地址: " + WiFi.localIP().toString() + "</p>";
    html += "<p>監控端口: 8080</p>";
    html += "<p>設備狀態: " + String(deviceInitialized ? "已初始化" : "未初始化") + "</p>";
    html += "<p>HomeKit狀態: " + String(homeKitInitialized ? "已就緒" : "未就緒") + "</p>";
    html += "<p>可用記憶體: " + String(ESP.getFreeHeap()) + " bytes</p>";
    html += "</div>";
    html += "<div style=\"text-align:center;\">";
    html += "<a href=\"/status\" class=\"button\">📊 詳細狀態</a>";
    html += "<a href=\"/wifi\" class=\"button\">📶 WiFi配置</a>";
    html += "<a href=\"/ota\" class=\"button\">🔄 OTA更新</a>";
    html += "</div></div></body></html>";
    webServer->send(200, "text/html", html);
  });
  
  // 詳細狀態頁面（HTML）
  webServer->on("/status", [](){
    String html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">";
    html += "<title>系統詳細狀態</title>";
    html += "<style>body{font-family:Arial,sans-serif;margin:20px;background:#f0f0f0;}";
    html += ".container{max-width:800px;margin:0 auto;background:white;padding:20px;border-radius:10px;}";
    html += ".status-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:20px;margin:20px 0;}";
    html += ".status-card{background:#f8f9fa;border:1px solid #dee2e6;padding:15px;border-radius:8px;}";
    html += ".status-card h3{margin:0 0 10px 0;color:#495057;border-bottom:2px solid #007cba;padding-bottom:5px;}";
    html += ".status-item{display:flex;justify-content:space-between;padding:5px 0;border-bottom:1px solid #e9ecef;}";
    html += ".status-item:last-child{border-bottom:none;}";
    html += ".status-label{font-weight:bold;color:#6c757d;}";
    html += ".status-value{color:#212529;}";
    html += ".status-good{color:#28a745;}";
    html += ".status-warning{color:#ffc107;}";
    html += ".status-error{color:#dc3545;}";
    html += ".refresh-btn{background:#007cba;color:white;border:none;padding:8px 15px;border-radius:5px;cursor:pointer;margin:10px 5px;}";
    html += ".refresh-btn:hover{background:#006ba6;}";
    html += "</style>";
    html += "<script>function refreshStatus(){location.reload();}</script>";
    html += "</head><body>";
    html += "<div class=\"container\">";
    html += "<h1>📊 系統詳細狀態</h1>";
    html += "<div style=\"text-align:center;margin:15px 0;\">";
    html += "<button class=\"refresh-btn\" onclick=\"refreshStatus()\">🔄 刷新狀態</button>";
    html += "<button class=\"refresh-btn\" onclick=\"window.open('/status-api','_blank')\">📋 JSON API</button>";
    html += "</div>";
    
    html += "<div class=\"status-grid\">";
    
    // WiFi狀態卡片
    html += "<div class=\"status-card\">";
    html += "<h3>🌐 網路連接</h3>";
    html += "<div class=\"status-item\">";
    html += "<span class=\"status-label\">WiFi SSID:</span>";
    html += "<span class=\"status-value status-good\">" + WiFi.SSID() + "</span>";
    html += "</div>";
    html += "<div class=\"status-item\">";
    html += "<span class=\"status-label\">IP地址:</span>";
    html += "<span class=\"status-value\">" + WiFi.localIP().toString() + "</span>";
    html += "</div>";
    html += "<div class=\"status-item\">";
    html += "<span class=\"status-label\">MAC地址:</span>";
    html += "<span class=\"status-value\">" + WiFi.macAddress() + "</span>";
    html += "</div>";
    html += "<div class=\"status-item\">";
    html += "<span class=\"status-label\">信號強度:</span>";
    int rssi = WiFi.RSSI();
    String rssiClass = (rssi > -50) ? "status-good" : (rssi > -70) ? "status-warning" : "status-error";
    html += "<span class=\"status-value " + rssiClass + "\">" + String(rssi) + " dBm</span>";
    html += "</div>";
    html += "<div class=\"status-item\">";
    html += "<span class=\"status-label\">網關:</span>";
    html += "<span class=\"status-value\">" + WiFi.gatewayIP().toString() + "</span>";
    html += "</div>";
    html += "</div>";
    
    // 系統狀態卡片
    html += "<div class=\"status-card\">";
    html += "<h3>💻 系統資源</h3>";
    html += "<div class=\"status-item\">";
    html += "<span class=\"status-label\">可用記憶體:</span>";
    uint32_t freeHeap = ESP.getFreeHeap();
    String heapClass = (freeHeap > 100000) ? "status-good" : (freeHeap > 50000) ? "status-warning" : "status-error";
    html += "<span class=\"status-value " + heapClass + "\">" + String(freeHeap) + " bytes</span>";
    html += "</div>";
    html += "<div class=\"status-item\">";
    html += "<span class=\"status-label\">晶片型號:</span>";
    html += "<span class=\"status-value\">ESP32-C3</span>";
    html += "</div>";
    html += "<div class=\"status-item\">";
    html += "<span class=\"status-label\">CPU頻率:</span>";
    html += "<span class=\"status-value\">" + String(ESP.getCpuFreqMHz()) + " MHz</span>";
    html += "</div>";
    html += "<div class=\"status-item\">";
    html += "<span class=\"status-label\">Flash大小:</span>";
    html += "<span class=\"status-value\">" + String(ESP.getFlashChipSize() / 1024 / 1024) + " MB</span>";
    html += "</div>";
    html += "<div class=\"status-item\">";
    html += "<span class=\"status-label\">運行時間:</span>";
    unsigned long uptime = millis();
    unsigned long days = uptime / 86400000;
    unsigned long hours = (uptime % 86400000) / 3600000;
    unsigned long minutes = (uptime % 3600000) / 60000;
    html += "<span class=\"status-value\">" + String(days) + "天 " + String(hours) + "時 " + String(minutes) + "分</span>";
    html += "</div>";
    html += "</div>";
    
    // HomeKit狀態卡片
    html += "<div class=\"status-card\">";
    html += "<h3>🏠 HomeKit狀態</h3>";
    html += "<div class=\"status-item\">";
    html += "<span class=\"status-label\">初始化狀態:</span>";
    String hkClass = homeKitInitialized ? "status-good" : "status-error";
    html += "<span class=\"status-value " + hkClass + "\">" + String(homeKitInitialized ? "✅ 已就緒" : "❌ 未就緒") + "</span>";
    html += "</div>";
    html += "<div class=\"status-item\">";
    html += "<span class=\"status-label\">設備狀態:</span>";
    String deviceClass = deviceInitialized ? "status-good" : "status-error";
    html += "<span class=\"status-value " + deviceClass + "\">" + String(deviceInitialized ? "✅ 已初始化" : "❌ 未初始化") + "</span>";
    html += "</div>";
    html += "<div class=\"status-item\">";
    html += "<span class=\"status-label\">HomeKit端口:</span>";
    html += "<span class=\"status-value\">1201</span>";
    html += "</div>";
    html += "<div class=\"status-item\">";
    html += "<span class=\"status-label\">配對代碼:</span>";
    html += "<span class=\"status-value\">11122333</span>";
    html += "</div>";
    html += "<div class=\"status-item\">";
    html += "<span class=\"status-label\">監控端口:</span>";
    html += "<span class=\"status-value\">8080</span>";
    html += "</div>";
    html += "</div>";
    
    // 溫控器狀態卡片
    html += "<div class=\"status-card\">";
    html += "<h3>🌡️ 溫控器狀態</h3>";
    if (thermostatController && deviceInitialized) {
      // 這裡可以添加溫控器的具體狀態
      html += "<div class=\"status-item\">";
      html += "<span class=\"status-label\">通訊協議:</span>";
      html += "<span class=\"status-value\">S21 協議版本1</span>";
      html += "</div>";
      html += "<div class=\"status-item\">";
      html += "<span class=\"status-label\">串口配置:</span>";
      html += "<span class=\"status-value\">2400 8E2</span>";
      html += "</div>";
      html += "<div class=\"status-item\">";
      html += "<span class=\"status-label\">當前溫度:</span>";
      html += "<span class=\"status-value\">21.0°C</span>";
      html += "</div>";
      html += "<div class=\"status-item\">";
      html += "<span class=\"status-label\">目標溫度:</span>";
      html += "<span class=\"status-value\">21.0°C</span>";
      html += "</div>";
      html += "<div class=\"status-item\">";
      html += "<span class=\"status-label\">運行模式:</span>";
      html += "<span class=\"status-value\">OFF</span>";
      html += "</div>";
    } else {
      html += "<div class=\"status-item\">";
      html += "<span class=\"status-label\">狀態:</span>";
      html += "<span class=\"status-value status-error\">❌ 設備未初始化</span>";
      html += "</div>";
    }
    html += "</div>";
    
    html += "</div>"; // end status-grid
    
    html += "<div style=\"text-align:center;margin:20px 0;\">";
    html += "<a href=\"/\" style=\"color:#007cba;text-decoration:none;\">⬅️ 返回主頁</a>";
    html += "</div>";
    
    html += "</div></body></html>";
    webServer->send(200, "text/html", html);
  });
  
  // JSON狀態API
  webServer->on("/status-api", [](){
    String json = "{";
    json += "\"wifi_ssid\":\"" + WiFi.SSID() + "\",";
    json += "\"wifi_ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"wifi_mac\":\"" + WiFi.macAddress() + "\",";
    json += "\"wifi_rssi\":" + String(WiFi.RSSI()) + ",";
    json += "\"wifi_gateway\":\"" + WiFi.gatewayIP().toString() + "\",";
    json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"cpu_freq\":" + String(ESP.getCpuFreqMHz()) + ",";
    json += "\"flash_size\":" + String(ESP.getFlashChipSize()) + ",";
    json += "\"homekit_initialized\":" + String(homeKitInitialized ? "true" : "false") + ",";
    json += "\"device_initialized\":" + String(deviceInitialized ? "true" : "false") + ",";
    json += "\"uptime\":" + String(millis()) + ",";
    json += "\"chip_model\":\"ESP32-C3\",";
    json += "\"homekit_port\":1201,";
    json += "\"monitor_port\":8080}";
    webServer->send(200, "application/json", json);
  });
  
  // OTA頁面
  webServer->on("/ota", [](){
    String html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">";
    html += "<title>OTA 更新</title>";
    html += "<style>body{font-family:Arial,sans-serif;margin:20px;background:#f0f0f0;}";
    html += ".container{max-width:500px;margin:0 auto;background:white;padding:20px;border-radius:10px;}";
    html += ".info{background:#fff3cd;border:1px solid #ffeaa7;padding:15px;border-radius:5px;margin:15px 0;}";
    html += "</style></head><body>";
    html += "<div class=\"container\"><h1>🔄 OTA 無線更新</h1>";
    html += "<div class=\"info\"><h3>📝 使用說明</h3>";
    html += "<p>使用 PlatformIO 進行 OTA 更新：</p>";
    html += "<code>pio run -t upload --upload-port " + WiFi.localIP().toString() + "</code>";
    html += "<p style=\"margin-top:15px;\">設備信息：</p>";
    html += "<p>主機名: DaiSpan-" + WiFi.macAddress() + "</p>";
    html += "<p>IP地址: " + WiFi.localIP().toString() + "</p>";
    html += "<p>監控端口: 8080</p>";
    html += "</div>";
    html += "<p><a href=\"/\">⬅️ 返回主頁</a></p>";
    html += "</div></body></html>";
    webServer->send(200, "text/html", html);
  });
  
  // WiFi配置頁面
  webServer->on("/wifi", [](){
    String html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">";
    html += "<title>WiFi 配置</title>";
    html += "<style>body{font-family:Arial,sans-serif;margin:20px;background:#f0f0f0;}";
    html += ".container{max-width:500px;margin:0 auto;background:white;padding:20px;border-radius:10px;}";
    html += ".network{background:#f8f9fa;border:1px solid #dee2e6;padding:10px;margin:5px 0;border-radius:5px;cursor:pointer;}";
    html += ".network:hover{background:#e9ecef;}";
    html += ".form-group{margin:15px 0;}";
    html += "label{display:block;margin-bottom:5px;font-weight:bold;}";
    html += "input[type=text],input[type=password]{width:100%;padding:8px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box;}";
    html += ".button{background:#007cba;color:white;padding:10px 20px;border:none;border-radius:5px;cursor:pointer;}";
    html += ".button:hover{background:#006ba6;}";
    html += ".warning{background:#fff3cd;border:1px solid #ffeaa7;padding:15px;border-radius:5px;margin:15px 0;}";
    html += "</style></head><body>";
    html += "<div class=\"container\"><h1>📶 WiFi 配置</h1>";
    html += "<div class=\"warning\">⚠️ 配置新WiFi後設備將重啟，HomeKit配對狀態會保持。</div>";
    
    // 掃描WiFi網路
    html += "<h3>可用網路 <button type=\"button\" class=\"button\" onclick=\"rescanNetworks()\" style=\"font-size:12px;padding:5px 10px;\">🔄 重新掃描</button></h3>";
    html += "<div id=\"networks\">";
    
    int networkCount = WiFi.scanNetworks();
    if (networkCount > 0) {
      for (int i = 0; i < networkCount && i < 10; i++) { // 最多顯示10個網路
        String ssid = WiFi.SSID(i);
        int rssi = WiFi.RSSI(i);
        String security = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "開放" : "加密";
        
        html += "<div class=\"network\" onclick=\"selectNetwork('" + ssid + "')\">";
        html += "<strong>" + ssid + "</strong> (" + security + ") 信號: " + String(rssi) + "dBm";
        html += "</div>";
      }
    } else {
      html += "<div style=\"padding:15px;text-align:center;color:#666;\">沒有找到WiFi網路，請點擊重新掃描</div>";
    }
    
    html += "</div>";
    html += "<form action=\"/wifi-save\" method=\"POST\">";
    html += "<div class=\"form-group\">";
    html += "<label for=\"ssid\">網路名稱 (SSID):</label>";
    html += "<input type=\"text\" id=\"ssid\" name=\"ssid\" required>";
    html += "</div>";
    html += "<div class=\"form-group\">";
    html += "<label for=\"password\">密碼:</label>";
    html += "<input type=\"password\" id=\"password\" name=\"password\">";
    html += "</div>";
    html += "<button type=\"submit\" class=\"button\">💾 保存並重啟</button>";
    html += "</form>";
    html += "<script>";
    html += "function selectNetwork(ssid) {";
    html += "  document.getElementById('ssid').value = ssid;";
    html += "}";
    html += "function rescanNetworks() {";
    html += "  var btn = document.querySelector('button');";
    html += "  btn.innerHTML = '⏳ 掃描中...';";
    html += "  btn.disabled = true;";
    html += "  fetch('/wifi-scan').then(response => response.text()).then(data => {";
    html += "    document.getElementById('networks').innerHTML = data;";
    html += "    btn.innerHTML = '🔄 重新掃描';";
    html += "    btn.disabled = false;";
    html += "  }).catch(error => {";
    html += "    console.error('掃描失敗:', error);";
    html += "    btn.innerHTML = '❌ 掃描失敗';";
    html += "    setTimeout(() => {";
    html += "      btn.innerHTML = '🔄 重新掃描';";
    html += "      btn.disabled = false;";
    html += "    }, 2000);";
    html += "  });";
    html += "}";
    html += "</script>";
    html += "<p><a href=\"/\">⬅️ 返回主頁</a></p>";
    html += "</div></body></html>";
    webServer->send(200, "text/html", html);
  });
  
  // WiFi掃描API
  webServer->on("/wifi-scan", [](){
    String html = "";
    
    DEBUG_INFO_PRINT("[Main] 開始WiFi掃描...\n");
    int networkCount = WiFi.scanNetworks();
    DEBUG_INFO_PRINT("[Main] 掃描完成，找到 %d 個網路\n", networkCount);
    
    if (networkCount > 0) {
      for (int i = 0; i < networkCount && i < 10; i++) { // 最多顯示10個網路
        String ssid = WiFi.SSID(i);
        int rssi = WiFi.RSSI(i);
        String security = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "開放" : "加密";
        
        html += "<div class=\"network\" onclick=\"selectNetwork('" + ssid + "')\">";
        html += "<strong>" + ssid + "</strong> (" + security + ") 信號: " + String(rssi) + "dBm";
        html += "</div>";
      }
    } else {
      html += "<div style=\"padding:15px;text-align:center;color:#666;\">沒有找到WiFi網路，請重試</div>";
    }
    
    webServer->send(200, "text/html", html);
  });
  
  // WiFi配置保存處理
  webServer->on("/wifi-save", HTTP_POST, [](){
    String ssid = webServer->arg("ssid");
    String password = webServer->arg("password");
    
    if (ssid.length() > 0) {
      DEBUG_INFO_PRINT("[Main] 保存新WiFi配置: SSID=%s\n", ssid.c_str());
      
      // 保存新的WiFi配置
      configManager.setWiFiCredentials(ssid, password);
      
      String html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">";
      html += "<title>配置已保存</title>";
      html += "<style>body{font-family:Arial,sans-serif;margin:20px;background:#f0f0f0;}";
      html += ".container{max-width:400px;margin:0 auto;background:white;padding:20px;border-radius:10px;text-align:center;}";
      html += ".success{background:#d4edda;border:1px solid #c3e6cb;padding:15px;border-radius:5px;margin:15px 0;}";
      html += "</style></head><body>";
      html += "<div class=\"container\">";
      html += "<h1>✅ 配置已保存</h1>";
      html += "<div class=\"success\">";
      html += "<p>新的WiFi配置已保存成功！</p>";
      html += "<p>設備將在3秒後重啟並嘗試連接到：<br><strong>" + ssid + "</strong></p>";
      html += "</div>";
      html += "<p>重啟後請等待設備重新連接，然後訪問新的IP地址。</p>";
      html += "</div>";
      html += "<script>setTimeout(function(){window.location='/restart';}, 3000);</script>";
      html += "</body></html>";
      webServer->send(200, "text/html", html);
    } else {
      webServer->send(400, "text/plain", "SSID不能為空");
    }
  });
  
  // 重啟端點
  webServer->on("/restart", [](){
    webServer->send(200, "text/plain", "設備重啟中...");
    delay(1000);
    safeRestart();
  });
  
  webServer->begin();
  monitoringEnabled = true;
  
  DEBUG_INFO_PRINT("[Main] WebServer監控功能已啟動: http://%s:8080\n", WiFi.localIP().toString().c_str());
}

// 初始化HomeKit功能（只有在WiFi穩定連接後調用）
void initializeHomeKit() {
  if (homeKitInitialized) {
    return;
  }
  
  DEBUG_INFO_PRINT("[Main] 開始初始化HomeKit...\n");
  
  // 使用固定的HomeKit配置
  homeSpan.setPairingCode("11122333");
  homeSpan.setStatusPin(2);
  homeSpan.setHostNameSuffix("");
  homeSpan.setQRID("HSPN");
  homeSpan.setPortNum(1201);        // 改變HomeSpan端口，讓WebServer使用8080
  // 注意：HomeSpan 2.1.2版本不支援setMaxConnections，使用預設TCP連接配置
  homeSpan.enableWebLog(50,"pool.ntp.org","UTC-8","log");

  // 初始化 HomeSpan
  homeSpan.begin(Category::Thermostats, "DaiSpan Thermostat");
  
  // 立即創建 HomeSpan 配件和服務
  accessory = new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Name("智能恆溫器");
  new Characteristic::Manufacturer("DaiSpan");
  new Characteristic::SerialNumber("123-ABC");
  new Characteristic::Model("恆溫器 v1.0");
  new Characteristic::FirmwareRevision("1.0");
  new Characteristic::Identify();
  
  // 創建ThermostatDevice（需要硬件已初始化）
  if (deviceInitialized && thermostatController) {
    thermostatDevice = new ThermostatDevice(*thermostatController);
    if (!thermostatDevice) {
      DEBUG_ERROR_PRINT("[Main] 創建 ThermostatDevice 失敗\n");
    } else {
      DEBUG_INFO_PRINT("[Main] ThermostatDevice 創建成功\n");
    }
  } else {
    DEBUG_ERROR_PRINT("[Main] 硬件未初始化，無法創建ThermostatDevice\n");
  }
  
  homeKitInitialized = true;
  DEBUG_INFO_PRINT("[Main] HomeKit配件初始化完成\n");
}

// 初始化硬件組件
void initializeHardware() {
  if (deviceInitialized) {
    DEBUG_INFO_PRINT("[Main] 硬件已經初始化\n");
    return;
  }
  
  DEBUG_INFO_PRINT("[Main] 開始初始化串口通訊...\n");
  
  // 初始化串口通訊
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
  
  deviceInitialized = true;
  DEBUG_INFO_PRINT("[Main] 硬件初始化完成\n");
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
    
    // WiFi連接成功，不啟動Web服務器，專注於HomeKit功能
    
    // 先初始化硬件組件
    initializeHardware();
    
    // 然後初始化HomeKit（硬件準備好後）
    initializeHomeKit();
    
    // 清理WiFi管理器，避免干擾HomeKit
    delete wifiManager;
    wifiManager = nullptr;
    
    // 初始化簡單的OTA功能
    ArduinoOTA.setHostname("DaiSpan-Thermostat");
    ArduinoOTA.begin();
    
    // 等待HomeKit完全穩定後再啟動監控
    delay(2000);
    initializeMonitoring();
    
    DEBUG_INFO_PRINT("[Main] HomeKit模式啟動，WebServer監控已啟用\n");
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

  // 根據模式處理不同的邏輯
  if (homeKitInitialized) {
    // HomeKit模式：處理HomeSpan、WebServer和OTA
    homeSpan.poll();
    if (monitoringEnabled && webServer) {
      webServer->handleClient();  // 處理Web請求
    }
    ArduinoOTA.handle();
  } else if (wifiManager) {
    // 配置模式：處理WiFi管理器
    wifiManager->loop();
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
    lastLoopTime = currentTime;
  }
}