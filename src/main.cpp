// DaiSpan 智能恆溫器系統

#include "HomeSpan.h"
#include "controller/ThermostatController.h"
#ifndef DISABLE_MOCK_CONTROLLER
#include "controller/MockThermostatController.h"
#endif
#include "device/ThermostatDevice.h"
#include "device/FanDevice.h"
#include "device/SwingDevice.h"
#include "protocol/S21Protocol.h"
#include "protocol/IACProtocol.h"
#include "protocol/ACProtocolFactory.h"
#include "common/Debug.h"
#include "common/Config.h"
#include "common/WiFiManager.h"
#include "common/SystemManager.h"
#include <ArduinoOTA.h>
#include "common/OTAManager.h"
#include "WiFi.h"
#include "WebServer.h"
#include "common/WebUI.h"
#include "common/RemoteDebugger.h"
#include "common/DebugWebClient.h"
#include "common/StreamingResponse.h"

// 硬體定義
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

// 核心系統全域變數
std::unique_ptr<ACProtocolFactory> protocolFactory = nullptr;
IThermostatControl* thermostatController = nullptr;
#ifndef DISABLE_MOCK_CONTROLLER
MockThermostatController* mockController = nullptr;
#endif
ThermostatDevice* thermostatDevice = nullptr;
FanDevice* fanDevice = nullptr;
SpanAccessory* accessory = nullptr;
bool deviceInitialized = false;
bool homeKitInitialized = false;

// 配置和管理器
ConfigManager configManager;
WiFiManager* wifiManager = nullptr;
OTAManager* otaManager = nullptr;
SystemManager* systemManager = nullptr;

// WebServer
WebServer* webServer = nullptr;
bool monitoringEnabled = false;
bool homeKitPairingActive = false;


// 函數聲明
void safeRestart();
void generateMainPage();
#ifndef DISABLE_SIMULATION_MODE
void generateSimulationPage();
#endif
void initializeMonitoring();
void initializeHomeKit();
void initializeHardware();


void safeRestart() {
    DEBUG_INFO_PRINT("[Main] 安全重啟...\n");
    delay(500);
    ESP.restart();
}

void generateMainPage() {
    if (!webServer) return;

    StreamingResponse stream;
    stream.begin(webServer);

    stream.append("<!DOCTYPE html><html><head><meta charset='UTF-8'>");
    stream.append("<title>DaiSpan</title>");
    stream.append("<meta http-equiv='refresh' content='30'>");
    stream.append("<style>");
    stream.append(WebUI::getCompactCSS());
    stream.append("</style></head><body>");
    stream.append("<div class='container'>");
    stream.append("<h1>DaiSpan</h1>");

    // 系統狀態
    stream.append("<div class='status-card'><h3>系統狀態</h3>");
    stream.appendf("<div class='status-item'><span class='status-label'>記憶體:</span>"
                   "<span class='status-value'>%u bytes</span></div>", ESP.getFreeHeap());

    unsigned long sec = millis() / 1000;
    unsigned long min = sec / 60;
    unsigned long hr = min / 60;
    stream.appendf("<div class='status-item'><span class='status-label'>運行時長:</span>"
                   "<span class='status-value'>%lu:%02lu:%02lu</span></div>", hr, min % 60, sec % 60);

    if (thermostatController) {
        bool on = thermostatController->getPower();
        stream.appendf("<div class='status-item'><span class='status-label'>電源:</span>"
                       "<span class='status-value %s'>%s</span></div>",
                       on ? "status-good" : "status-warning", on ? "開啟" : "關閉");
        stream.appendf("<div class='status-item'><span class='status-label'>溫度:</span>"
                       "<span class='status-value'>%.1f / %.1f °C</span></div>",
                       thermostatController->getCurrentTemperature(),
                       thermostatController->getTargetTemperature());
    }

    if (WiFi.status() == WL_CONNECTED) {
        stream.appendf("<div class='status-item'><span class='status-label'>WiFi:</span>"
                       "<span class='status-value status-good'>%s (%d dBm)</span></div>",
                       WiFi.localIP().toString().c_str(), WiFi.RSSI());
    }
    stream.append("</div>");

    // 導航
    stream.append("<div style='margin-top:20px;text-align:center;'>");
    stream.append("<a href='/wifi' class='button'>WiFi</a>");
    stream.append("<a href='/homekit' class='button'>HomeKit</a>");
#ifndef DISABLE_SIMULATION_MODE
    if (configManager.getSimulationMode() && mockController) {
        stream.append("<a href='/simulation' class='button'>模擬控制</a>");
    } else {
        stream.append("<a href='/simulation-toggle' class='button secondary'>模擬模式</a>");
    }
#endif
    stream.append("<a href='/ota' class='button secondary'>OTA</a>");
    stream.append("<a href='/debug' class='button secondary'>Debug</a>");
    stream.append("</div>");

    stream.append("</div></body></html>");
    stream.finish();
}

#ifndef DISABLE_SIMULATION_MODE
void generateSimulationPage() {
    if (!webServer || !mockController) return;

    StreamingResponse stream;
    stream.begin(webServer);

    stream.append("<!DOCTYPE html><html><head><meta charset='UTF-8'>");
    stream.append("<title>模擬控制</title><style>");
    stream.append(WebUI::getCompactCSS());
    stream.append("</style></head><body><div class='container'>");
    stream.append("<h1>模擬控制台</h1>");

    // 狀態
    bool on = mockController->getPower();
    const char* status = mockController->isSimulationHeating() ? "加熱中" :
                        (mockController->isSimulationCooling() ? "制冷中" : "待機");
    stream.appendf("<p>電源: <b>%s</b> | 溫度: <b>%.1f / %.1f °C</b> | %s</p>",
                   on ? "ON" : "OFF",
                   mockController->getCurrentTemperature(),
                   mockController->getTargetTemperature(), status);

    // 表單
    stream.append("<form method='post' action='/simulation-control'>");
    stream.appendf("<p><label>電源: <select name='power'>"
                   "<option value='0'%s>關閉</option><option value='1'%s>開啟</option></select></label></p>",
                   !on ? " selected" : "", on ? " selected" : "");

    int mode = mockController->getTargetMode();
    stream.append("<p><label>模式: <select name='mode'>");
    const char* modeNames[] = {"關閉", "制熱", "制冷", "自動"};
    for (int i = 0; i < 4; i++)
        stream.appendf("<option value='%d'%s>%s</option>", i, mode == i ? " selected" : "", modeNames[i]);
    stream.append("</select></label></p>");

    stream.appendf("<p><label>目標溫度: <input type='number' name='target_temp' min='16' max='30' step='0.5' value='%.1f'></label></p>",
                   mockController->getTargetTemperature());
    stream.appendf("<p><label>房間溫度: <input type='number' name='room_temp' min='10' max='40' step='0.5' value='%.1f'></label></p>",
                   mockController->getSimulatedRoomTemp());

    int fan = mockController->getFanSpeed();
    stream.append("<p><label>風速: <select name='fan_speed'>");
    stream.appendf("<option value='0'%s>自動</option>", fan == 0 ? " selected" : "");
    for (int i = 1; i <= 5; i++)
        stream.appendf("<option value='%d'%s>%d檔</option>", i, fan == i ? " selected" : "", i);
    stream.append("</select></label></p>");

    stream.append("<p style='text-align:center'><button type='submit' class='button'>套用</button> "
                  "<a href='/' class='button secondary'>返回</a></p>");
    stream.append("</form></div></body></html>");
    stream.finish();
}
#endif


// WebServer 初始化函數
void initializeMonitoring() {
    if (monitoringEnabled || !homeKitInitialized) {
        return;
    }
    
    DEBUG_INFO_PRINT("[Main] 啟動WebServer (記憶體: %u bytes)\n", ESP.getFreeHeap());
    
    if (!webServer) {
        webServer = new WebServer(8080);
        if (!webServer) {
            DEBUG_ERROR_PRINT("[Main] WebServer創建失敗\n");
            return;
        }
    }
    
    // 基本路由處理 - 使用記憶體優化
    webServer->on("/", [](){
        webServer->sendHeader("Cache-Control", "no-cache, must-revalidate");
        generateMainPage();
    });
    
    // WiFi配置頁面
    webServer->on("/wifi", [](){
        String html = WebUI::getSimpleWiFiConfigPage();
        webServer->send(200, "text/html", html);
    });
    
    // WiFi掃描API
    webServer->on("/wifi-scan", [](){
        DEBUG_INFO_PRINT("[Main] 開始WiFi掃描...\n");
        int networkCount = WiFi.scanNetworks();
        
        String json = "[";
        int validNetworks = 0;
        
        if (networkCount > 0) {
            for (int i = 0; i < networkCount && i < 15; i++) {
                String ssid = WiFi.SSID(i);
                if (ssid.length() == 0) continue;
                
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
        webServer->send(200, "application/json", json);
    });
    
    // WiFi配置保存處理
    webServer->on("/wifi-save", HTTP_POST, [](){
        String ssid = webServer->arg("ssid");
        String password = webServer->arg("password");
        
        if (ssid.length() > 0) {
            configManager.setWiFiCredentials(ssid, password);
            String message = "新的WiFi配置已保存成功！設備將重啟並嘗試連接。";
            String html = WebUI::getSuccessPage("WiFi配置已保存", message, 3, "/restart");
            webServer->send(200, "text/html", html);
        } else {
            webServer->send(400, "text/plain", "SSID不能為空");
        }
    });
    
    // HomeKit配置頁面
    webServer->on("/homekit", [](){
        String currentPairingCode = configManager.getHomeKitPairingCode();
        String currentDeviceName = configManager.getHomeKitDeviceName();
        String currentQRID = configManager.getHomeKitQRID();
        
        String html = WebUI::getHomeKitConfigPage("/homekit-save", currentPairingCode, 
                                                 currentDeviceName, currentQRID, homeKitInitialized);
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
        
        if (pairingCode.length() > 0 && pairingCode != currentPairingCode) {
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
            } else {
                webServer->send(400, "text/plain", "配對碼必須是8位數字");
                return;
            }
        }
        
        if (deviceName.length() > 0 && deviceName != currentDeviceName) {
            currentDeviceName = deviceName;
            configChanged = true;
        }
        
        if (qrId.length() > 0 && qrId != currentQRID) {
            currentQRID = qrId;
            configChanged = true;
        }
        
        if (configChanged) {
            configManager.setHomeKitConfig(currentPairingCode, currentDeviceName, currentQRID);
            String message = "配置更新成功！設備將重啟並應用新配置。";
            String html = WebUI::getSuccessPage("HomeKit配置已保存", message, 3, "/restart");
            webServer->send(200, "text/html", html);
        } else {
            String message = "您沒有修改任何配置。";
            String html = WebUI::getSuccessPage("無需更新", message, 0);
            webServer->send(200, "text/html", html);
        }
    });
    
    #ifndef DISABLE_SIMULATION_MODE
    // 模擬控制頁面
    webServer->on("/simulation", [](){
        if (!configManager.getSimulationMode()) {
            webServer->send(403, "text/plain", "模擬功能未啟用");
            return;
        }
        
        if (!mockController) {
            webServer->send(500, "text/plain", "模擬控制器不可用");
            return;
        }
        
        generateSimulationPage();
    });
    
    // 模擬控制處理
    webServer->on("/simulation-control", HTTP_POST, [](){
        if (!configManager.getSimulationMode() || !mockController) {
            webServer->send(403, "text/plain", "模擬功能不可用");
            return;
        }
        
        String powerStr = webServer->arg("power");
        String modeStr = webServer->arg("mode");
        String targetTempStr = webServer->arg("target_temp");
        String currentTempStr = webServer->arg("current_temp");
        String roomTempStr = webServer->arg("room_temp");
        String fanSpeedStr = webServer->arg("fan_speed");
        
        // 電源控制
        if (powerStr.length() > 0) {
            bool power = (powerStr.toInt() == 1);
            mockController->setPower(power);
            if (!power) {
                mockController->setTargetMode(0);
            }
        }
        
        // 模式控制
        if (modeStr.length() > 0 && mockController->getPower()) {
            uint8_t mode = modeStr.toInt();
            if (mode >= 0 && mode <= 3) {
                mockController->setTargetMode(mode);
            }
        }
        
        // 溫度設定
        if (targetTempStr.length() > 0 && mockController->getPower()) {
            float targetTemp = targetTempStr.toFloat();
            if (targetTemp >= 16.0f && targetTemp <= 30.0f) {
                mockController->setTargetTemperature(targetTemp);
            }
        }
        
        if (currentTempStr.length() > 0) {
            float currentTemp = currentTempStr.toFloat();
            if (currentTemp >= 10.0f && currentTemp <= 40.0f) {
                mockController->setCurrentTemperature(currentTemp);
            }
        }
        
        if (roomTempStr.length() > 0) {
            float roomTemp = roomTempStr.toFloat();
            if (roomTemp >= 10.0f && roomTemp <= 40.0f) {
                mockController->setSimulatedRoomTemp(roomTemp);
            }
        }
        
        // 風量控制
        if (fanSpeedStr.length() > 0 && mockController->getPower()) {
            uint8_t fanSpeed = fanSpeedStr.toInt();
            if (fanSpeed >= 0 && fanSpeed <= 6) {
                mockController->setFanSpeed(fanSpeed);
            }
        }
        
        String message = "模擬參數已成功更新！";
        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>設置已更新</title>";
        html += "<style>" + String(WebUI::getCompactCSS()) + "</style></head><body>";
        html += "<div class='container'><h1>✅ 設置已更新</h1>";
        html += "<div class='status'>" + message + "</div>";
        html += "<div style='text-align:center;margin:20px 0;'>";
        html += "<a href='/simulation' class='button'>🔧 返回模擬控制</a>&nbsp;&nbsp;";
        html += "<a href='/' class='button secondary'>🏠 返回主頁</a>";
        html += "</div></div></body></html>";
        webServer->send(200, "text/html", html);
    });
    
    // 模式切換頁面
    webServer->on("/simulation-toggle", [](){
        bool currentMode = configManager.getSimulationMode();
        String html = WebUI::getSimulationTogglePage("/simulation-toggle-confirm", currentMode);
        webServer->send(200, "text/html", html);
    });
    
    // 模式切換確認
    webServer->on("/simulation-toggle-confirm", HTTP_POST, [](){
        bool currentMode = configManager.getSimulationMode();
        configManager.setSimulationMode(!currentMode);
        
        String message = "運行模式已切換，設備將重啟。";
        String html = WebUI::getSuccessPage("模式切換中", message, 3, "/restart");
        webServer->send(200, "text/html", html);
    });
    #endif // DISABLE_SIMULATION_MODE
    
    // 系統健康檢查端點
    webServer->on("/api/health", [](){
        char buf[128];
        snprintf(buf, sizeof(buf),
            "{\"status\":\"ok\",\"freeHeap\":%u,\"uptime\":%u}",
            ESP.getFreeHeap(), (uint32_t)(millis() / 1000));
        webServer->send(200, "application/json", buf);
    });

    webServer->on("/api/metrics", [](){
        static char buffer[1024];
        
        // 收集數據到局部變量
        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t heapSize = ESP.getHeapSize();
        uint32_t uptime = millis() / 1000;
        float memUsage = (float)(freeHeap) / (float)(heapSize) * 100.0f;
        
        // 構建 JSON 字符串
        int written = snprintf(buffer, sizeof(buffer),
            "{"
            "\"performance\":{"
            "\"uptime\":%u,"
            "\"freeHeap\":%u,"
            "\"heapSize\":%u,"
            "\"memoryUsage\":%.1f,"
            "\"cpuFreq\":%u,"
            "\"flashSize\":%u,"
            "\"minFreeHeap\":%u,"
            "\"maxAllocHeap\":%u,"
            "\"sketchSize\":%u,"
            "\"freeSketchSpace\":%u"
            "},"
            "\"network\":{"
            "\"rssi\":%d,"
            "\"ip\":\"%s\","
            "\"mac\":\"%s\","
            "\"channel\":%d,"
            "\"hostname\":\"%s\""
            "}",
            uptime, freeHeap, heapSize, memUsage,
            ESP.getCpuFreqMHz(), ESP.getFlashChipSize(),
            ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(),
            ESP.getSketchSize(), ESP.getFreeSketchSpace(),
            WiFi.RSSI(), WiFi.localIP().toString().c_str(),
            WiFi.macAddress().c_str(), WiFi.channel(),
            WiFi.getHostname()
        );
        
        // 添加 HomeKit 指標
        if (thermostatDevice && thermostatController && written < sizeof(buffer) - 200) {
            written += snprintf(buffer + written, sizeof(buffer) - written,
                ",\"homekit\":{"
                "\"power\":%s,"
                "\"targetMode\":%d,"
                "\"currentTemp\":%.1f,"
                "\"targetTemp\":%.1f,"
                "\"initialized\":%s,"
                "\"pairingActive\":%s"
                "}",
                thermostatController->getPower() ? "true" : "false",
                thermostatController->getTargetMode(),
                thermostatController->getCurrentTemperature(),
                thermostatController->getTargetTemperature(),
                homeKitInitialized ? "true" : "false",
                homeKitPairingActive ? "true" : "false"
            );
        }
        
        // 添加時間戳並結束
        if (written < sizeof(buffer) - 50) {
            snprintf(buffer + written, sizeof(buffer) - written,
                ",\"timestamp\":%u}", uptime);
        }
        
        webServer->send(200, "application/json", buffer);
    });
    
    // OTA 頁面
    webServer->on("/ota", [](){
        String deviceIP = WiFi.localIP().toString();
        String html = WebUI::getOTAPage(deviceIP, "DaiSpan-Thermostat", "");
        webServer->send(200, "text/html", html);
    });
    
    // 記憶體清理 API 端點
    webServer->on("/api/memory/stats", [](){
        char buffer[256];
        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t maxAlloc = ESP.getMaxAllocHeap();
        uint32_t fragmentation = (maxAlloc > 0) ? (100 - (maxAlloc * 100 / freeHeap)) : 0;

        snprintf(buffer, sizeof(buffer),
            "{"
            "\"freeHeap\":%u,"
            "\"maxAllocHeap\":%u,"
            "\"fragmentation\":%u,"
            "\"timestamp\":%u"
            "}",
            freeHeap, maxAlloc, fragmentation, (uint32_t)(millis() / 1000)
        );
        webServer->send(200, "application/json", buffer);
    });
    
    // Controller 狀態端點
    webServer->on("/api/controller", [](){
        char buffer[256];
        if (thermostatController) {
            bool healthy = true;
            unsigned long errors = 0;
            #ifndef DISABLE_MOCK_CONTROLLER
            if (!configManager.getSimulationMode()) {
            #endif
                auto* tc = static_cast<ThermostatController*>(thermostatController);
                healthy = tc->isProtocolHealthy();
                errors = tc->getConsecutiveErrors();
            #ifndef DISABLE_MOCK_CONTROLLER
            }
            #endif
            snprintf(buffer, sizeof(buffer),
                "{"
                "\"healthy\":%s,"
                "\"errors\":%lu,"
                "\"power\":%s,"
                "\"mode\":%d,"
                "\"targetTemp\":%.1f,"
                "\"currentTemp\":%.1f,"
                "\"fanSpeed\":%d"
                "}",
                healthy ? "true" : "false",
                errors,
                thermostatController->getPower() ? "true" : "false",
                thermostatController->getTargetMode(),
                thermostatController->getTargetTemperature(),
                thermostatController->getCurrentTemperature(),
                thermostatController->getFanSpeed()
            );
        } else {
            snprintf(buffer, sizeof(buffer), "{\"error\":\"no controller\"}");
        }
        webServer->send(200, "application/json", buffer);
    });

    // 重啟端點
    webServer->on("/restart", [](){
        String html = WebUI::getRestartPage(WiFi.localIP().toString() + ":8080");
        webServer->send(200, "text/html", html);
        delay(1000);
        safeRestart();
    });
    
    // 遠端調試界面
    webServer->on("/debug", [](){
        String html = DebugWebClient::getDebugHTML();
        webServer->send(200, "text/html", html);
    });
    
    // 404 處理
    webServer->onNotFound([](){
        webServer->sendHeader("Connection", "close");
        webServer->send(404, "text/plain", "Not Found");
    });
    
    webServer->begin();
    monitoringEnabled = true;
    
    DEBUG_INFO_PRINT("[Main] WebServer已啟動: http://%s:8080\n", 
                     WiFi.localIP().toString().c_str());
    
    // 初始化遠端調試系統（生產環境中禁用以節省記憶體）
#ifndef PRODUCTION_BUILD
    RemoteDebugger& debugger = RemoteDebugger::getInstance();
    if (debugger.begin(8081)) {
        DEBUG_INFO_PRINT("[Main] 遠端調試系統已啟動: ws://%s:8081\n", 
                         WiFi.localIP().toString().c_str());
        DEBUG_INFO_PRINT("[Main] 調試界面: http://%s:8080/debug\n", 
                         WiFi.localIP().toString().c_str());
    } else {
        DEBUG_ERROR_PRINT("[Main] 遠端調試系統啟動失敗\n");
    }
#else
    DEBUG_INFO_PRINT("[Main] 生產模式：遠端調試系統已禁用以節省記憶體\n");
#endif
}

// 現有的初始化函數（保持不變）
void initializeHomeKit() {
    // [保持原有 HomeKit 初始化代碼]
    if (homeKitInitialized) {
        return;
    }
    
    DEBUG_INFO_PRINT("[Main] 開始初始化HomeKit...\n");
    
    String pairingCode = configManager.getHomeKitPairingCode();
    String deviceName = configManager.getHomeKitDeviceName();
    String qrId = configManager.getHomeKitQRID();
    
    homeSpan.setPairingCode(pairingCode.c_str());
    homeSpan.setHostNameSuffix("-DaiSpan");
    homeSpan.setQRID(qrId.c_str());
    homeSpan.setPortNum(1201);
    homeSpan.setLogLevel(1);
    homeSpan.setControlPin(0);
    homeSpan.setStatusPin(2);

    DEBUG_INFO_PRINT("[Main] HomeKit配置 - 配對碼: %s, 設備名稱: %s\n",
                     pairingCode.c_str(), deviceName.c_str());
    
    DEBUG_INFO_PRINT("[Main] 開始HomeSpan初始化...\n");
    homeSpan.begin(Category::Thermostats, deviceName.c_str());
    DEBUG_INFO_PRINT("[Main] HomeSpan初始化完成\n");
    
    accessory = new SpanAccessory();
    new Service::AccessoryInformation();
    new Characteristic::Name("Thermostat");
    new Characteristic::Manufacturer("DaiSpan");
    new Characteristic::SerialNumber("123");
    new Characteristic::Model("TH1.0");
    new Characteristic::FirmwareRevision("1.0");
    new Characteristic::Identify();
    
    if (deviceInitialized && thermostatController) {
        DEBUG_INFO_PRINT("[Main] 硬件已初始化，創建ThermostatDevice和FanDevice\n");
        
        thermostatDevice = new ThermostatDevice(*thermostatController);
        if (!thermostatDevice) {
            DEBUG_ERROR_PRINT("[Main] 創建 ThermostatDevice 失敗\n");
        } else {
            DEBUG_INFO_PRINT("[Main] ThermostatDevice 創建成功並註冊到HomeKit\n");
        }
        
        fanDevice = new FanDevice(*thermostatController);
        if (!fanDevice) {
            DEBUG_ERROR_PRINT("[Main] 創建 FanDevice 失敗\n");
        } else {
            DEBUG_INFO_PRINT("[Main] FanDevice 創建成功並註冊到HomeKit\n");
        }

        // 擺風開關（作為獨立 Switch 服務掛在同一個 Accessory 上）
        if (thermostatController->supportsSwing(IACProtocol::SwingAxis::Vertical)) {
            new SwingSwitchService(*thermostatController, IACProtocol::SwingAxis::Vertical, "擺風");
            DEBUG_INFO_PRINT("[Main] 垂直擺風開關已註冊到HomeKit\n");
        }
    } else {
        DEBUG_ERROR_PRINT("[Main] 硬件未初始化，無法創建HomeKit設備\n");
    }
    
    homeKitInitialized = true;
    DEBUG_INFO_PRINT("[Main] HomeKit配件初始化完成\n");
}

void initializeHardware() {
    // [保持原有硬件初始化代碼]
    if (deviceInitialized) {
        DEBUG_INFO_PRINT("[Main] 硬件已經初始化\n");
        return;
    }
    
#ifndef DISABLE_SIMULATION_MODE
    bool simulationMode = configManager.getSimulationMode();
    
    if (simulationMode) {
        DEBUG_INFO_PRINT("[Main] 啟用模擬模式 - 創建模擬控制器\n");
        
        mockController = new MockThermostatController(25.0f);
        if (!mockController) {
            DEBUG_ERROR_PRINT("[Main] MockThermostatController 創建失敗\n");
            return;
        }
        
        thermostatController = static_cast<IThermostatControl*>(mockController);
        deviceInitialized = true;
        DEBUG_INFO_PRINT("[Main] 模擬模式初始化完成\n");
        
    } else 
#endif
    {
        DEBUG_INFO_PRINT("[Main] 啟用真實模式 - 初始化串口通訊...\n");
        
        Serial1.begin(2400, SERIAL_8E2, S21_RX_PIN, S21_TX_PIN);
        delay(200);
        
        protocolFactory = ACProtocolFactory::createFactory();
        if (!protocolFactory) {
            DEBUG_ERROR_PRINT("[Main] 協議工廠創建失敗\n");
            return;
        }
        
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

void setup() {
    Serial.begin(115200);
    DEBUG_INFO_PRINT("\n[Main] DaiSpan 智能恆溫器啟動...\n");
    
    DEBUG_INFO_PRINT("[Main] 可用堆內存: %d bytes\n", ESP.getFreeHeap());

    // 初始化配置管理器
    configManager.begin();

    // 初始化WiFi管理器
    wifiManager = new WiFiManager(configManager);
    
    // 檢查WiFi配置狀態
    bool hasWiFiConfig = configManager.isWiFiConfigured();
    
    if (!hasWiFiConfig) {
        DEBUG_INFO_PRINT("[Main] 未找到WiFi配置，啟動配置模式\n");
        wifiManager->begin();
        DEBUG_INFO_PRINT("[Main] 請連接到 DaiSpan-Config 進行WiFi配置\n");
    } else {
        DEBUG_INFO_PRINT("[Main] 找到WiFi配置，嘗試連接...\n");
        
        String ssid = configManager.getWiFiSSID();
        String password = configManager.getWiFiPassword();
        
        WiFi.mode(WIFI_STA);
        delay(100);
        
        #if defined(ESP32C3_SUPER_MINI)
            WiFi.setTxPower(WIFI_POWER_11dBm);
        #endif
        
        WiFi.begin(ssid.c_str(), password.c_str());
        DEBUG_INFO_PRINT("[Main] 開始WiFi連接，使用漸進式重試策略...\n");
        
        // WiFi 連接邏輯（保持原有）
        int attempts = 0;
        int maxAttempts = 25;
        unsigned long connectStartTime = millis();
        
        while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
            if (attempts < 10) {
                delay(500);
            } else if (attempts < 20) {
                delay(1000);
            } else {
                delay(2000);
            }
            
            DEBUG_VERBOSE_PRINT(".");
            attempts++;
            
            if (attempts % 5 == 0) {
                DEBUG_INFO_PRINT("\n[Main] WiFi連接嘗試 %d/%d (已用時 %lu 秒)\n", 
                               attempts, maxAttempts, (millis() - connectStartTime) / 1000);
            }
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            DEBUG_INFO_PRINT("\n[Main] WiFi連接成功: %s\n", WiFi.localIP().toString().c_str());
            
            #if defined(ESP32C3_SUPER_MINI)
                delay(1000);
                WiFi.setTxPower(WIFI_POWER_8_5dBm);
                DEBUG_INFO_PRINT("[Main] ESP32-C3 切換到節能模式 (8.5dBm)\n");
            #endif
            
            // Arduino OTA 設置（保持原有）
            ArduinoOTA.setHostname("DaiSpan-Thermostat");
            ArduinoOTA.setPassword("12345678");
            ArduinoOTA.setPort(3232);
            
            ArduinoOTA.onStart([]() {
                String type;
                if (ArduinoOTA.getCommand() == U_FLASH) {
                    type = "sketch";
                } else {
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
            
            // 先初始化硬件組件
            initializeHardware();
            
            // 然後初始化HomeKit（硬件準備好後）
            initializeHomeKit();
            
            DEBUG_INFO_PRINT("[Main] HomeKit模式啟動，WebServer監控將延遲啟動\n");
        } else {
            DEBUG_ERROR_PRINT("\n[Main] WiFi連接失敗，啟動配置模式\n");
            wifiManager->begin();
        }
    }
    
    // 統一的SystemManager初始化
    systemManager = new SystemManager(
        configManager, wifiManager, webServer,
        thermostatController, 
        #ifndef DISABLE_MOCK_CONTROLLER
        mockController, 
        #else
        nullptr,
        #endif
        thermostatDevice,
        deviceInitialized, homeKitInitialized, monitoringEnabled, homeKitPairingActive
    );
    
    DEBUG_INFO_PRINT("[Main] 系統管理器初始化完成\n");
    
    // 清理WiFiManager（如果在HomeKit模式）
    if (homeKitInitialized && wifiManager) {
        if (wifiManager->isInAPMode()) {
            wifiManager->stopAPMode();
            delay(500);
        }
        
        delete wifiManager;
        wifiManager = nullptr;
        DEBUG_INFO_PRINT("[Main] WiFiManager已清理，進入純HomeKit模式\n");
    }
    
}

void loop() {
    // 處理遠端調試器
    RemoteDebugger::getInstance().loop();
    
    
    // 使用系統管理器處理主迴圈邏輯
    if (systemManager) {
        systemManager->processMainLoop();
        
        // 檢查是否需要啟動監控
        if (systemManager->shouldStartMonitoring()) {
            DEBUG_INFO_PRINT("[Main] 系統管理器請求啟動監控\n");
            initializeMonitoring();
        }
        
        // 處理配置模式（WiFi管理器）
        if (wifiManager) {
            if (WiFi.status() == WL_CONNECTED && !homeKitInitialized && !deviceInitialized) {
                DEBUG_INFO_PRINT("[Main] WiFi已連接，開始初始化HomeKit...\n");
                
                if (wifiManager->isInAPMode()) {
                    wifiManager->stopAPMode();
                    delay(500);
                }
                
                delete wifiManager;
                wifiManager = nullptr;
                
                initializeHardware();
                initializeHomeKit();
                
                DEBUG_INFO_PRINT("[Main] HomeKit初始化完成\n");
            } else {
                wifiManager->loop();
            }
        }
    } else {
        // 降級處理
        DEBUG_ERROR_PRINT("[Main] 系統管理器未初始化，使用降級模式\n");
        
        if (WiFi.status() == WL_CONNECTED) {
            ArduinoOTA.handle();
        }
        
        if (homeKitInitialized) {
            homeSpan.poll();
        }
        
        if (wifiManager) {
            wifiManager->loop();
        }
        
        delay(50);
    }
}