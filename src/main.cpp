// DaiSpan V3 架構整合版本
// 這是 main.cpp 的 V3 版本，整合了事件驅動架構和現有功能

#include "HomeSpan.h"
#include "controller/ThermostatController.h"
#include "controller/MockThermostatController.h"
#include "device/ThermostatDevice.h"
#include "device/FanDevice.h"
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
#include "ArduinoOTA.h"
#include "common/WebUI.h"
#include "common/WebTemplates.h"
#include "common/RemoteDebugger.h"
#include "common/DebugWebClient.h"

// V3 架構組件
#ifdef V3_ARCHITECTURE_ENABLED
#include "architecture_v3/core/EventSystemSimple.h"
#include "architecture_v3/core/ServiceContainerSimple.h"
#include "architecture_v3/domain/ThermostatDomain.h"
#include "architecture_v3/domain/ConfigDomain.h"
#include "migration/V3MigrationAdapter.h"
#include <Preferences.h>
#endif

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

// 現有全域變數 (V2 系統)
std::unique_ptr<ACProtocolFactory> protocolFactory = nullptr;
IThermostatControl* thermostatController = nullptr;
MockThermostatController* mockController = nullptr;
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

#ifdef V3_ARCHITECTURE_ENABLED
// V3 全域組件
DaiSpan::Core::EventPublisher* g_eventBus = nullptr;
DaiSpan::Core::ServiceContainer* g_serviceContainer = nullptr;
DaiSpan::Migration::MigrationManager* g_migrationManager = nullptr;
Preferences g_preferences;
bool v3ArchitectureEnabled = false;
#endif

// 安全重啟函數
void safeRestart() {
    DEBUG_INFO_PRINT("[Main] 開始安全重啟...\n");
    
#ifdef V3_ARCHITECTURE_ENABLED
    // V3 清理
    if (g_migrationManager) {
        DEBUG_INFO_PRINT("[V3] 清理遷移管理器\n");
        delete g_migrationManager;
        g_migrationManager = nullptr;
    }
    
    if (g_serviceContainer) {
        DEBUG_INFO_PRINT("[V3] 清理服務容器\n");
        delete g_serviceContainer;
        g_serviceContainer = nullptr;
    }
    
    if (g_eventBus) {
        DEBUG_INFO_PRINT("[V3] 清理事件總線\n");
        delete g_eventBus;
        g_eventBus = nullptr;
    }
#endif
    
    delay(500);
    ESP.restart();
}

#ifdef V3_ARCHITECTURE_ENABLED
/**
 * 初始化 V3 架構
 */
void setupV3Architecture() {
    DEBUG_INFO_PRINT("[V3] 初始化 V3 架構...\n");
    
    try {
        // 1. 初始化事件系統
        g_eventBus = new DaiSpan::Core::EventPublisher();
        if (!g_eventBus) {
            DEBUG_ERROR_PRINT("[V3] 事件總線創建失敗\n");
            return;
        }
        
        // 確保統計數據從零開始
        g_eventBus->resetStatistics();
        DEBUG_INFO_PRINT("[V3] 事件總線統計已重置\n");
        
        // 2. 初始化服務容器
        g_serviceContainer = new DaiSpan::Core::ServiceContainer();
        if (!g_serviceContainer) {
            DEBUG_ERROR_PRINT("[V3] 服務容器創建失敗\n");
            return;
        }
        
        // 3. 初始化偏好設定（用於 V3 配置）
        if (!g_preferences.begin("daispan_v3", false)) {
            DEBUG_ERROR_PRINT("[V3] V3 偏好設定初始化失敗\n");
            return;
        }
        
        // 4. 註冊配置服務
        g_serviceContainer->registerFactory<DaiSpan::Domain::Config::ConfigurationManager>(
            "ConfigurationManager",
            [](DaiSpan::Core::ServiceContainer& container) -> std::shared_ptr<DaiSpan::Domain::Config::ConfigurationManager> {
                return std::make_shared<DaiSpan::Domain::Config::ConfigurationManager>(g_preferences);
            });
        
        // 5. 初始化遷移管理器
        g_migrationManager = new DaiSpan::Migration::MigrationManager(*g_eventBus, *g_serviceContainer);
        if (!g_migrationManager) {
            DEBUG_ERROR_PRINT("[V3] 遷移管理器創建失敗\n");
            return;
        }
        
        DEBUG_INFO_PRINT("[V3] V3 基礎架構初始化完成\n");
        v3ArchitectureEnabled = true;
        
    } catch (const std::exception& e) {
        DEBUG_ERROR_PRINT("[V3] V3 架構初始化異常: %s\n", e.what());
        v3ArchitectureEnabled = false;
    }
}

/**
 * 設置 V3 遷移橋接
 */
void setupV3Migration() {
    if (!v3ArchitectureEnabled || !g_migrationManager) {
        DEBUG_WARN_PRINT("[V3] V3 架構未啟用，跳過遷移設置\n");
        return;
    }
    
    if (!thermostatController) {
        DEBUG_WARN_PRINT("[V3] 恆溫器控制器未初始化，延後遷移設置\n");
        return;
    }
    
    DEBUG_INFO_PRINT("[V3] 設置 V2 到 V3 遷移橋接...\n");
    
    // 設置橋接適配器（連接 V2 和 V3）
    auto migrationResult = g_migrationManager->initialize(thermostatController, thermostatDevice);
    if (migrationResult.isFailure()) {
        DEBUG_ERROR_PRINT("[V3] 遷移橋接初始化失敗: %s\n", migrationResult.getError().c_str());
        return;
    }
    
    // 設置事件監聽器（用於調試和監控）
    g_eventBus->subscribe("StateChanged",
        [](const DaiSpan::Core::DomainEvent& event) {
            DEBUG_VERBOSE_PRINT("[V3] 狀態變化事件接收\n");
            REMOTE_WEBLOG("[V3-Event] 恆溫器狀態變化");
        });
    
    g_eventBus->subscribe("CommandReceived",
        [](const DaiSpan::Core::DomainEvent& event) {
            DEBUG_VERBOSE_PRINT("[V3] 命令接收事件\n");
            REMOTE_WEBLOG("[V3-Event] 命令接收");
        });
    
    g_eventBus->subscribe("TemperatureUpdated",
        [](const DaiSpan::Core::DomainEvent& event) {
            DEBUG_INFO_PRINT("[V3] 溫度更新事件\n");
            REMOTE_WEBLOG("[V3-Event] 溫度更新");
        });
    
    g_eventBus->subscribe("Error",
        [](const DaiSpan::Core::DomainEvent& event) {
            DEBUG_ERROR_PRINT("[V3] 領域錯誤事件\n");
            REMOTE_WEBLOG("[V3-Error] 系統錯誤");
        });
    
    DEBUG_INFO_PRINT("[V3] V2/V3 遷移橋接設置完成\n");
}

/**
 * 處理 V3 事件（在主循環中調用）
 */
void processV3Events() {
    if (!v3ArchitectureEnabled || !g_migrationManager) {
        return;
    }
    
    // 處理事件總線
    g_migrationManager->processEvents();
    
    // 定期輸出統計資訊（每 60 秒）
    static unsigned long lastStatsTime = 0;
    if (millis() - lastStatsTime > 60000) {
        if (g_eventBus) {
            // auto stats = g_eventBus->getStatistics();
            DEBUG_VERBOSE_PRINT("[V3] 事件統計 - 佇列: %zu\n", g_eventBus->getQueueSize());
            
            if (g_eventBus->getQueueSize() > 10) {
                DEBUG_WARN_PRINT("[V3] 事件佇列積壓過多: %zu\n", g_eventBus->getQueueSize());
            }
        }
        lastStatsTime = millis();
    }
}

/**
 * 獲取 V3 狀態資訊（用於 WebServer API）
 */
String getV3StatusInfo() {
    if (!v3ArchitectureEnabled) {
        return "\"v3Architecture\":false";
    }
    
    String info = "\"v3Architecture\":true";
    
    if (g_eventBus) {
        info += ",\"eventBus\":{";
        info += "\"queueSize\":" + String(g_eventBus->getQueueSize()) + ",";
        info += "\"subscriptions\":" + String(g_eventBus->getSubscriptionCount()) + ",";
        info += "\"processed\":" + String(g_eventBus->getProcessedEventCount());
        info += "}";
    }
    
    if (g_migrationManager) {
        info += ",\"migration\":{";
        info += "\"active\":" + String(g_migrationManager->isMigrationActive() ? "true" : "false");
        
        auto thermostatAggregate = g_migrationManager->getThermostatAggregate();
        if (thermostatAggregate) {
            info += ",\"aggregateReady\":" + String(thermostatAggregate->isReady() ? "true" : "false");
        } else {
            info += ",\"aggregateReady\":false";
        }
        info += "}";
    }
    
    return info;
}
#endif

// 現有的 WebServer 初始化函數（修改版本以支援 V3 狀態）
void initializeMonitoring() {
    if (monitoringEnabled || !homeKitInitialized) {
        return;
    }
    
    DEBUG_INFO_PRINT("[Main] 啟動WebServer監控功能...\n");
    DEBUG_INFO_PRINT("[Main] 可用記憶體: %d bytes\n", ESP.getFreeHeap());
    
    // 檢查記憶體是否足夠
    if (ESP.getFreeHeap() < 70000) {
        DEBUG_ERROR_PRINT("[Main] 記憶體不足(%d bytes)，跳過WebServer啟動\n", ESP.getFreeHeap());
        return;
    }
    
    if (!webServer) {
        webServer = new WebServer(8080);
        if (!webServer) {
            DEBUG_ERROR_PRINT("[Main] WebServer創建失敗\n");
            return;
        }
    }
    
    // 現有的路由處理...（保持原有功能）
    webServer->on("/", [](){
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
        webServer->sendHeader("Connection", "close");
        
        const size_t bufferSize = 4096;
        auto buffer = std::make_unique<char[]>(bufferSize);
        if (!buffer) {
            webServer->send(500, "text/plain", "Memory allocation failed");
            return;
        }

        char* p = buffer.get();
        int remaining = bufferSize;
        bool overflow = false;
        auto append = [&](const char* format, ...) {
            if (remaining <= 10 || overflow) {
                overflow = true;
                return;
            }
            va_list args;
            va_start(args, format);
            int written = vsnprintf(p, remaining, format, args);
            va_end(args);
            if (written > 0 && written < remaining) {
                p += written;
                remaining -= written;
            } else {
                overflow = true;
            }
        };
        
        // HTML生成 - 加入V3狀態顯示
        append("<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>DaiSpan 智能恆溫器</title>");
        append("<meta http-equiv=\"refresh\" content=\"30\">");
        append("<style>%s</style></head><body>", WebUI::getCompactCSS());
        append("<div class=\"container\"><h1>DaiSpan 智能恆溫器</h1>");
        
#ifdef V3_ARCHITECTURE_ENABLED
        // V3 狀態卡片
        if (v3ArchitectureEnabled) {
            append("<div class=\"card\"><h3>🚀 V3 架構狀態</h3>");
            append("<p><strong>架構版本:</strong> V3 事件驅動</p>");
            if (g_eventBus) {
                // auto stats = g_eventBus->getStatistics();
                append("<p><strong>事件統計:</strong> 佇列:%zu 訂閱:%zu 已處理:%zu</p>", 
                       g_eventBus->getQueueSize(), 
                       g_eventBus->getSubscriptionCount(),
                       g_eventBus->getProcessedEventCount());
            }
            if (g_migrationManager && g_migrationManager->isMigrationActive()) {
                append("<p><strong>遷移狀態:</strong> ✅ 活躍</p>");
            }
            append("</div>");
        }
#endif
        
        // 系統狀態資訊
        String statusCard = WebUI::getSystemStatusCard();
        append("%s", statusCard.c_str());
        
        // 按鈕區域
        append("<div style=\"text-align:center;margin:20px 0;\">");
        append("<a href=\"/wifi\" class=\"button\">WiFi配置</a>");
        append("<a href=\"/homekit\" class=\"button\">HomeKit設定</a>");
        if (configManager.getSimulationMode()) {
            append("<a href=\"/simulation\" class=\"button\">模擬控制</a>");
        }
        append("<a href=\"/simulation-toggle\" class=\"button\">切換%s模式</a>", 
               configManager.getSimulationMode() ? "真實" : "模擬");
        append("<a href=\"/debug\" class=\"button\">遠端調試</a>");
        append("<a href=\"/ota\" class=\"button\">OTA更新</a></div></div></body></html>");
        
        if (overflow) {
            webServer->send(500, "text/html", "<div style='color:red;'>Error: HTML too large for buffer</div>");
            return;
        }
        
        String html(buffer.get());
        webServer->send(200, "text/html", html);
    });
    
    // 修改的 JSON狀態API，包含 V3 資訊
    webServer->on("/status-api", [](){
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
        
#ifdef V3_ARCHITECTURE_ENABLED
        // 在 JSON 結尾前插入 V3 狀態
        if (json.endsWith("}")) {
            json = json.substring(0, json.length() - 1); // 移除最後的 '}'
            json += "," + getV3StatusInfo() + "}";
        }
#endif
        
        webServer->send(200, "application/json", json);
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
        
        String html = WebUI::getSimulationControlPage("/simulation-control",
                                                     mockController->getPower(),
                                                     mockController->getTargetMode(),
                                                     mockController->getTargetTemperature(),
                                                     mockController->getCurrentTemperature(),
                                                     mockController->getSimulatedRoomTemp(),
                                                     mockController->isSimulationHeating(),
                                                     mockController->isSimulationCooling(),
                                                     mockController->getFanSpeed());
        webServer->send(200, "text/html", html);
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
    
#ifdef V3_ARCHITECTURE_ENABLED
    // V3 調試端點 - 手動觸發事件測試
    webServer->on("/v3-test-event", [](){
        if (!v3ArchitectureEnabled || !g_eventBus) {
            webServer->send(400, "text/plain", "V3 架構未啟用");
            return;
        }
        
        try {
            // 觸發一個測試事件
            auto testEvent = std::make_unique<DaiSpan::Domain::Thermostat::Events::CommandReceived>(
                DaiSpan::Domain::Thermostat::Events::CommandReceived::Type::Temperature,
                "debug-test",
                "手動測試事件"
            );
            
            g_eventBus->publish(std::move(testEvent));
            
            String response = "✅ V3 測試事件已發布!\n";
            response += "佇列大小: " + String(g_eventBus->getQueueSize()) + "\n";
            response += "訂閱數: " + String(g_eventBus->getSubscriptionCount()) + "\n"; 
            response += "已處理: " + String(g_eventBus->getProcessedEventCount()) + "\n";
            
            webServer->send(200, "text/plain", response);
            
            DEBUG_INFO_PRINT("[V3-Debug] 手動觸發測試事件\n");
            
        } catch (...) {
            webServer->send(500, "text/plain", "❌ 事件發布失敗");
        }
    });
    
    // V3 統計 API
    webServer->on("/api/v3/stats", [](){
        if (!v3ArchitectureEnabled || !g_eventBus) {
            webServer->send(400, "application/json", "{\"error\":\"V3 architecture not enabled\"}");
            return;
        }
        
        String json = "{";
        json += "\"queueSize\":" + String(g_eventBus->getQueueSize()) + ",";
        json += "\"subscriptions\":" + String(g_eventBus->getSubscriptionCount()) + ",";
        json += "\"processed\":" + String(g_eventBus->getProcessedEventCount()) + ",";
        json += "\"migration\":" + String(g_migrationManager && g_migrationManager->isMigrationActive() ? "true" : "false");
        json += "}";
        
        webServer->send(200, "application/json", json);
    });
    
    // V3 統計重置端點
    webServer->on("/api/v3/reset-stats", [](){
        if (!v3ArchitectureEnabled || !g_eventBus) {
            webServer->send(400, "application/json", "{\"error\":\"V3 architecture not enabled\"}");
            return;
        }
        
        g_eventBus->resetStatistics();
        String json = "{\"status\":\"success\",\"message\":\"Statistics reset successfully\"}";
        webServer->send(200, "application/json", json);
    });
#endif
    
    // OTA 頁面
    webServer->on("/ota", [](){
        String deviceIP = WiFi.localIP().toString();
        String html = WebUI::getOTAPage(deviceIP, "DaiSpan-Thermostat", "");
        webServer->send(200, "text/html", html);
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
    
    DEBUG_INFO_PRINT("[Main] WebServer監控功能已啟動: http://%s:8080\n", 
                     WiFi.localIP().toString().c_str());
    
    // 初始化遠端調試系統
    RemoteDebugger& debugger = RemoteDebugger::getInstance();
    if (debugger.begin(8081)) {
        DEBUG_INFO_PRINT("[Main] 遠端調試系統已啟動: ws://%s:8081\n", 
                         WiFi.localIP().toString().c_str());
        DEBUG_INFO_PRINT("[Main] 調試界面: http://%s:8080/debug\n", 
                         WiFi.localIP().toString().c_str());
    } else {
        DEBUG_ERROR_PRINT("[Main] 遠端調試系統啟動失敗\n");
    }
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
    homeSpan.setStatusPin(2);
    homeSpan.setHostNameSuffix("");
    homeSpan.setQRID(qrId.c_str());
    homeSpan.setPortNum(1201);
    
    DEBUG_INFO_PRINT("[Main] HomeKit配置 - 配對碼: %s, 設備名稱: %s\n", 
                     pairingCode.c_str(), deviceName.c_str());

    homeSpan.setLogLevel(1);
    homeSpan.setControlPin(0);
    homeSpan.setStatusPin(2);
    
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
        
#ifdef V3_ARCHITECTURE_ENABLED
        // HomeKit 初始化完成後，設置 V3 遷移
        setupV3Migration();
#endif
        
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
        
    } else {
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
    Serial.begin(115200);
    DEBUG_INFO_PRINT("\n[Main] DaiSpan V3 架構版本啟動...\n");
    
#ifdef V3_ARCHITECTURE_ENABLED
    DEBUG_INFO_PRINT("[Main] V3 架構支援已啟用\n");
    
    // 初始化 V3 架構
    setupV3Architecture();
#else
    DEBUG_INFO_PRINT("[Main] V3 架構支援未啟用，使用 V2 模式\n");
#endif
    
    // 原有的設置程序（保持不變）
    #if defined(ESP32C3_SUPER_MINI)
        #ifdef HIGH_PERFORMANCE_WIFI
            WiFi.setTxPower(WIFI_POWER_19_5dBm);
            DEBUG_INFO_PRINT("[Main] WiFi 高性能模式已啟用\n");
        #else
            WiFi.setTxPower(WIFI_POWER_8_5dBm);
            DEBUG_INFO_PRINT("[Main] WiFi 節能模式已啟用\n");
        #endif
        
        DEBUG_INFO_PRINT("[Main] 可用堆內存: %d bytes\n", ESP.getFreeHeap());
    #endif

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
        thermostatController, mockController, thermostatDevice,
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
    
#ifdef V3_ARCHITECTURE_ENABLED
    if (v3ArchitectureEnabled) {
        DEBUG_INFO_PRINT("[V3] V3 架構設置完成，混合模式運行\n");
    }
#endif
}

void loop() {
    // 處理遠端調試器
    RemoteDebugger::getInstance().loop();
    
#ifdef V3_ARCHITECTURE_ENABLED
    // V3 事件處理（優先處理）
    processV3Events();
#endif
    
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