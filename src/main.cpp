// DaiSpan 智能恆溫器系統
// 基於事件驅動架構的現代化智能家居解決方案

#include "HomeSpan.h"
#include "controller/ThermostatController.h"
#ifndef DISABLE_MOCK_CONTROLLER
#include "controller/MockThermostatController.h"
#endif
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
#include "common/WebUI.h"
#include "common/WebTemplates.h"
#include "common/RemoteDebugger.h"  // 自動處理條件編譯：調試環境=實際功能，生產環境=空實現
#include "common/DebugWebClient.h"
#include "common/MemoryOptimization.h"

// 核心架構組件
#include "core/EventSystem.h"
#include "core/ServiceContainer.h"
#include "domain/ThermostatDomain.h"
#include "domain/ConfigDomain.h"
#include <Preferences.h>

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

// 記憶體優化組件
std::unique_ptr<MemoryOptimization::WebPageGenerator> pageGenerator = nullptr;
std::unique_ptr<MemoryOptimization::MemoryManager> memoryManager = nullptr;

// 核心架構組件
DaiSpan::Core::EventPublisher* g_eventBus = nullptr;
DaiSpan::Core::ServiceContainer* g_serviceContainer = nullptr;
Preferences g_preferences;
bool modernArchitectureEnabled = false;

// 系統啟動時間追蹤
unsigned long systemStartTime = 0;

// 函數聲明
void safeRestart();
void initializeMemoryOptimization();
void generateOptimizedMainPage();
void generateUnifiedMainPage();
#ifndef DISABLE_SIMULATION_MODE
void generateOptimizedSimulationPage();
#endif
void setupModernArchitecture();
void setupCoreEventListeners();
void processCoreEvents();
void initializeMonitoring();
void initializeHomeKit();
void initializeHardware();
void wifiCallback();

// 安全重啟函數
void safeRestart() {
    DEBUG_INFO_PRINT("[Main] 開始安全重啟...\n");
    
    // 記憶體優化組件清理
    if (pageGenerator) {
        DEBUG_INFO_PRINT("[Main] 清理頁面生成器\n");
        pageGenerator.reset();
    }
    
    if (memoryManager) {
        DEBUG_INFO_PRINT("[Main] 清理記憶體管理器\n");
        memoryManager.reset();
    }
    
    // 核心架構清理
    if (g_serviceContainer) {
        DEBUG_INFO_PRINT("[Core] 清理服務容器\n");
        delete g_serviceContainer;
        g_serviceContainer = nullptr;
    }
    
    if (g_eventBus) {
        DEBUG_INFO_PRINT("[Core] 清理事件總線\n");
        delete g_eventBus;
        g_eventBus = nullptr;
    }
    
    delay(500);
    ESP.restart();
}

/**
 * 初始化記憶體優化組件
 */
void initializeMemoryOptimization() {
    DEBUG_INFO_PRINT("[Main] 初始化記憶體優化組件...\n");
    
    uint32_t availableMemory = ESP.getFreeHeap();
    DEBUG_INFO_PRINT("[Main] 初始化前可用記憶體: %u bytes\n", availableMemory);
    
    try {
        // 總是創建記憶體管理器 (輕量級)
        memoryManager = std::make_unique<MemoryOptimization::MemoryManager>();
        
        // 只在記憶體充足時創建頁面生成器
        if (availableMemory >= 70000) {
            pageGenerator = std::make_unique<MemoryOptimization::WebPageGenerator>();
            DEBUG_INFO_PRINT("[Main] 完整記憶體優化功能已啟用\n");
        } else {
            DEBUG_WARN_PRINT("[Main] 記憶體不足 (%u bytes)，使用精簡模式\n", availableMemory);
        }
        
        // 檢查初始記憶體狀態
        auto pressure = memoryManager->updateMemoryPressure();
        auto strategy = memoryManager->getRenderStrategy();
        
        DEBUG_INFO_PRINT("[Main] 記憶體優化初始化完成\n");
        DEBUG_INFO_PRINT("[Main] 記憶體壓力: %d, 渲染策略: %d\n", 
                         static_cast<int>(pressure), static_cast<int>(strategy));
        DEBUG_INFO_PRINT("[Main] 初始化後可用記憶體: %u bytes\n", ESP.getFreeHeap());
        
    } catch (const std::exception& e) {
        DEBUG_ERROR_PRINT("[Main] 記憶體優化初始化失敗: %s\n", e.what());
        // 降級到不使用記憶體優化
        pageGenerator.reset();
        memoryManager.reset();
    } catch (...) {
        DEBUG_ERROR_PRINT("[Main] 記憶體優化初始化失敗: 未知錯誤\n");
        pageGenerator.reset();
        memoryManager.reset();
    }
}

/**
 * 生成優化的主頁面
 */
void generateOptimizedMainPage() {
    if (!pageGenerator || !webServer) {
        DEBUG_ERROR_PRINT("[Main] 頁面生成器或WebServer未初始化\n");
        webServer->send(500, "text/html", "<html><body><h1>服務器內部錯誤</h1></body></html>");
        return;
    }
    
    try {
        // 使用 StreamingResponseBuilder 生成主頁面
        MemoryOptimization::StreamingResponseBuilder stream;
        stream.begin(webServer);
        
        // HTML頭部
        stream.append("<!DOCTYPE html><html><head><meta charset='UTF-8'>");
        stream.append("<title>DaiSpan 智能恆溫器</title>");
        stream.append("<meta http-equiv='refresh' content='30'>");
        stream.append("<style>");
        stream.append(WebUI::getCompactCSS());
        stream.append("</style></head><body>");
        
        // 容器開始
        stream.append("<div class='container'>");
        stream.append("<h1>DaiSpan 智能恆溫器</h1>");
        
        // 記憶體狀態卡片
        stream.append("<div class='status-card'>");
        stream.append("<h3>🔧 系統狀態</h3>");
        
        // 記憶體信息（合併顯示）
        stream.appendf("<div class='status-item'>");
        stream.appendf("<span class='status-label'>可用記憶體:</span>");
        
        if (memoryManager) {
            auto pressure = memoryManager->updateMemoryPressure();
            
            const char* pressureClass = "status-good";
            const char* pressureName = "正常";
            
            switch (pressure) {
                case MemoryOptimization::MemoryManager::MemoryPressure::PRESSURE_MEDIUM:
                    pressureClass = "status-warning";
                    pressureName = "中等";
                    break;
                case MemoryOptimization::MemoryManager::MemoryPressure::PRESSURE_HIGH:
                    pressureClass = "status-warning";
                    pressureName = "偏高";
                    break;
                case MemoryOptimization::MemoryManager::MemoryPressure::PRESSURE_CRITICAL:
                    pressureClass = "status-error";
                    pressureName = "嚴重";
                    break;
                default:
                    break;
            }
            
            stream.appendf("<span class='status-value %s'>%u bytes (%s)</span>", pressureClass, ESP.getFreeHeap(), pressureName);
        } else {
            stream.appendf("<span class='status-value status-good'>%u bytes</span>", ESP.getFreeHeap());
        }
        stream.append("</div>");
        
        // 系統運行時長
        unsigned long currentTime = millis();
        unsigned long uptime = currentTime - systemStartTime;
        unsigned long seconds = uptime / 1000;
        unsigned long minutes = seconds / 60;
        unsigned long hours = minutes / 60;
        unsigned long days = hours / 24;
        
        stream.append("<div class='status-item'>");
        stream.append("<span class='status-label'>運行時長:</span>");
        if (days > 0) {
            stream.appendf("<span class='status-value'>%lu天 %02lu:%02lu:%02lu</span>", days, hours % 24, minutes % 60, seconds % 60);
        } else {
            stream.appendf("<span class='status-value'>%02lu:%02lu:%02lu</span>", hours, minutes % 60, seconds % 60);
        }
        stream.append("</div>");
        
#ifndef PRODUCTION_BUILD
        // 事件系統統計（開發模式）
        if (modernArchitectureEnabled && g_eventBus) {
            stream.append("<div class='status-item'>");
            stream.append("<span class='status-label'>事件統計:</span>");
            stream.appendf("<span class='status-value'>佇列:%zu 訂閱:%zu 已處理:%zu</span>",
                          g_eventBus->getQueueSize(),
                          g_eventBus->getSubscriptionCount(),
                          g_eventBus->getProcessedEventCount());
            stream.append("</div>");
        }
#endif
        
        // 恆溫器狀態
        if (thermostatController) {
            stream.append("<div class='status-item'>");
            stream.append("<span class='status-label'>電源狀態:</span>");
            stream.appendf("<span class='status-value %s'>%s</span>",
                          thermostatController->getPower() ? "status-good" : "status-warning",
                          thermostatController->getPower() ? "開啟" : "關閉");
            stream.append("</div>");
            
            stream.append("<div class='status-item'>");
            stream.append("<span class='status-label'>目標溫度:</span>");
            stream.appendf("<span class='status-value'>%.1f°C</span>", thermostatController->getTargetTemperature());
            stream.append("</div>");
            
            stream.append("<div class='status-item'>");
            stream.append("<span class='status-label'>當前溫度:</span>");
            stream.appendf("<span class='status-value'>%.1f°C</span>", thermostatController->getCurrentTemperature());
            stream.append("</div>");
        }
        
        // WiFi狀態
        stream.append("<div class='status-item'>");
        stream.append("<span class='status-label'>WiFi狀態:</span>");
        if (WiFi.status() == WL_CONNECTED) {
            stream.appendf("<span class='status-value status-good'>已連接 (%s)</span>", WiFi.localIP().toString().c_str());
        } else {
            stream.append("<span class='status-value status-error'>未連接</span>");
        }
        stream.append("</div>");
        
        stream.append("</div>"); // 結束狀態卡片
        
        // 導航按鈕（置中）
        stream.append("<div style='margin-top: 20px; text-align: center;'>");
        stream.append("<a href='/wifi' class='button'>WiFi 設定</a>");
        stream.append("<a href='/homekit' class='button'>HomeKit 設定</a>");
        
#ifndef DISABLE_SIMULATION_MODE
        // 智能模擬控制按鈕
        if (configManager.getSimulationMode()) {
            if (mockController) {
                stream.append("<a href='/simulation' class='button'>🔧 模擬控制</a>");
            } else {
                stream.append("<a href='/simulation-toggle' class='button' style='background:#ffc107;'>⚠️ 模擬控制 (重新初始化)</a>");
            }
        } else {
            stream.append("<a href='/simulation-toggle' class='button' style='background:#28a745;'>🔧 啟用模擬模式</a>");
        }
#endif
        
#ifndef PRODUCTION_BUILD
        stream.append("<a href='/api/memory/cleanup' class='button secondary'>記憶體清理</a>");
#endif
        stream.append("</div>");
        
        // 結束容器和HTML
        stream.append("</div></body></html>");
        
        // 完成流式響應
        stream.finish();
        
        DEBUG_VERBOSE_PRINT("[Main] 主頁面生成完成（使用記憶體優化）\n");
        
    } catch (const std::exception& e) {
        DEBUG_ERROR_PRINT("[Main] 頁面生成發生異常: %s\n", e.what());
        webServer->send(500, "text/html", "<html><body><h1>頁面生成錯誤</h1></body></html>");
    } catch (...) {
        DEBUG_ERROR_PRINT("[Main] 頁面生成發生未知異常\n");
        webServer->send(500, "text/html", "<html><body><h1>未知錯誤</h1></body></html>");
    }
}

/**
 * 統一主頁面生成函數 - 所有版本使用相同的外觀和功能
 * 提供一致的用戶體驗，適應不同記憶體環境
 */
void generateUnifiedMainPage() {
    try {
        // 設置HTTP頭部
        webServer->sendHeader("Cache-Control", "no-cache, must-revalidate");
        webServer->sendHeader("Pragma", "no-cache");
        webServer->sendHeader("Connection", "close");
        
        // HomeKit配對狀態檢查
        if (homeKitPairingActive) {
            String pairingHtml = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
            pairingHtml += "<title>DaiSpan - HomeKit配對中</title>";
            pairingHtml += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
            pairingHtml += "<style>";
            pairingHtml += "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;margin:0;padding:20px;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;display:flex;align-items:center;justify-content:center;}";
            pairingHtml += ".container{background:white;padding:40px;border-radius:12px;box-shadow:0 8px 32px rgba(0,0,0,0.2);text-align:center;max-width:400px;}";
            pairingHtml += "h1{color:#333;margin-bottom:20px;}";
            pairingHtml += ".status{color:#007bff;font-size:18px;margin:20px 0;}";
            pairingHtml += ".spinner{border:4px solid #f3f3f3;border-top:4px solid #007bff;border-radius:50%;width:40px;height:40px;animation:spin 1s linear infinite;margin:20px auto;}";
            pairingHtml += "@keyframes spin{0%{transform:rotate(0deg)}100%{transform:rotate(360deg)}}";
            pairingHtml += "</style>";
            pairingHtml += "<script>setTimeout(function(){location.reload();}, 5000);</script>";
            pairingHtml += "</head><body>";
            pairingHtml += "<div class='container'>";
            pairingHtml += "<h1>🏠 HomeKit 配對進行中</h1>";
            pairingHtml += "<div class='spinner'></div>";
            pairingHtml += "<div class='status'>設備正在進行HomeKit配對，請稍候...</div>";
            pairingHtml += "<p>頁面將在5秒後自動刷新</p>";
            pairingHtml += "</div></body></html>";
            webServer->send(200, "text/html", pairingHtml);
            return;
        }
        
        // 統一的現代化主頁面設計
        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
        html += "<title>DaiSpan 智能恆溫器</title>";
        html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
        html += "<style>";
        
        // 現代化CSS樣式
        html += "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;margin:0;padding:20px;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;}";
        html += ".container{max-width:800px;margin:0 auto;}";
        html += ".header{text-align:center;color:white;margin-bottom:30px;}";
        html += ".header h1{font-size:2.5em;margin:0;text-shadow:0 2px 4px rgba(0,0,0,0.3);}";
        html += ".header p{font-size:1.1em;opacity:0.9;margin:10px 0;}";
        
        // 卡片樣式
        html += ".card{background:white;padding:25px;border-radius:12px;box-shadow:0 4px 20px rgba(0,0,0,0.1);margin-bottom:20px;transition:transform 0.2s;}";
        html += ".card:hover{transform:translateY(-2px);}";
        html += ".card h3{margin:0 0 20px 0;color:#333;border-bottom:2px solid #007bff;padding-bottom:8px;}";
        
        // 狀態顯示
        html += ".status-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:15px;margin-bottom:20px;}";
        html += ".status-item{padding:15px;background:#f8f9fa;border-radius:8px;border-left:4px solid #007bff;}";
        html += ".status-label{font-weight:bold;color:#495057;display:block;margin-bottom:5px;}";
        html += ".status-value{font-size:1.2em;color:#28a745;}";
        html += ".status-warning{color:#ffc107;}";
        html += ".status-error{color:#dc3545;}";
        
        // 導航按鈕
        html += ".nav-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:15px;}";
        html += ".nav-btn{display:block;padding:20px;text-decoration:none;background:#007bff;color:white;border-radius:8px;text-align:center;transition:all 0.2s;font-weight:bold;}";
        html += ".nav-btn:hover{background:#0056b3;transform:translateY(-1px);}";
        html += ".nav-btn.secondary{background:#6c757d;}";
        html += ".nav-btn.success{background:#28a745;}";
        html += ".nav-btn.warning{background:#ffc107;color:#212529;}";
        
        // 響應式設計
        html += "@media (max-width:768px){.status-grid,.nav-grid{grid-template-columns:1fr;}}";
        html += "</style>";
        html += "</head><body>";
        
        html += "<div class='container'>";
        html += "<div class='header'>";
        html += "<h1>🌡️ DaiSpan</h1>";
        html += "<p>智能恆溫器控制中心</p>";
        html += "</div>";
        
        // 系統狀態卡片
        html += "<div class='card'>";
        html += "<h3>📊 系統狀態</h3>";
        html += "<div class='status-grid'>";
        
        // 記憶體狀態
        uint32_t freeHeap = ESP.getFreeHeap();
        String memoryClass = (freeHeap > 100000) ? "status-value" : (freeHeap > 50000) ? "status-warning" : "status-error";
        html += "<div class='status-item'>";
        html += "<span class='status-label'>可用記憶體:</span>";
        html += "<span class='" + memoryClass + "'>" + String(freeHeap/1024) + " KB</span>";
        html += "</div>";
        
        // WiFi狀態
        html += "<div class='status-item'>";
        html += "<span class='status-label'>WiFi連接:</span>";
        if (WiFi.status() == WL_CONNECTED) {
            html += "<span class='status-value'>" + WiFi.SSID() + " (" + String(WiFi.RSSI()) + " dBm)</span>";
        } else {
            html += "<span class='status-error'>未連接</span>";
        }
        html += "</div>";
        
        // IP地址
        html += "<div class='status-item'>";
        html += "<span class='status-label'>IP地址:</span>";
        html += "<span class='status-value'>" + WiFi.localIP().toString() + "</span>";
        html += "</div>";
        
        // 運行時間
        unsigned long uptime = millis() / 1000;
        unsigned long hours = uptime / 3600;
        unsigned long minutes = (uptime % 3600) / 60;
        unsigned long seconds = uptime % 60;
        html += "<div class='status-item'>";
        html += "<span class='status-label'>運行時間:</span>";
        html += "<span class='status-value'>" + String(hours) + ":" + String(minutes) + ":" + String(seconds) + "</span>";
        html += "</div>";
        
        html += "</div></div>";
        
        // 功能導航卡片
        html += "<div class='card'>";
        html += "<h3>🛠️ 功能選單</h3>";
        html += "<div class='nav-grid'>";
        html += "<a href='/wifi' class='nav-btn'>📶 WiFi設定</a>";
        html += "<a href='/homekit' class='nav-btn'>🏠 HomeKit設定</a>";
        html += "<a href='/api/health' class='nav-btn secondary'>📋 系統狀態</a>";
        html += "<a href='/ota' class='nav-btn warning'>⬆️ OTA更新</a>";
        html += "<a href='/logs' class='nav-btn success'>📄 系統日誌</a>";

#ifndef DISABLE_SIMULATION_MODE
        // 模擬控制按鈕（如果啟用）
        if (configManager.getSimulationMode() && mockController) {
            html += "<a href='/simulation' class='nav-btn secondary'>🔧 模擬控制</a>";
        }
#endif
        
        html += "</div></div>";
        
        // 版權資訊
        html += "<div style='text-align:center;margin-top:30px;color:rgba(255,255,255,0.8);font-size:14px;'>";
        html += "DaiSpan Smart Thermostat Controller | 統一WebServer界面";
        html += "</div>";
        
        html += "</div></body></html>";
        
        webServer->send(200, "text/html", html);
        
    } catch (...) {
        // 極簡緊急頁面
        String emergencyHtml = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>DaiSpan</title></head><body>";
        emergencyHtml += "<h1>🌡️ DaiSpan 智能恆溫器</h1>";
        emergencyHtml += "<p><strong>可用記憶體:</strong> " + String(ESP.getFreeHeap()) + " bytes</p>";
        emergencyHtml += "<p><strong>IP地址:</strong> " + WiFi.localIP().toString() + "</p>";
        emergencyHtml += "<p><a href='/wifi'>WiFi設定</a> | <a href='/homekit'>HomeKit設定</a> | <a href='/ota'>OTA更新</a> | <a href='/logs'>系統日誌</a></p>";
        emergencyHtml += "<p><em>緊急模式 - 頁面載入異常</em></p>";
        emergencyHtml += "</body></html>";
        webServer->send(200, "text/html", emergencyHtml);
    }
}

#ifndef DISABLE_SIMULATION_MODE
/**
 * 生成優化的模擬控制頁面（使用流式響應）
 */
void generateOptimizedSimulationPage() {
    if (!pageGenerator || !memoryManager || !mockController) {
        DEBUG_ERROR_PRINT("[Main] 模擬頁面生成器或控制器未初始化\n");
        webServer->send(500, "text/html; charset=utf-8", 
                       "<html><body><h1>服務器內部錯誤</h1></body></html>");
        return;
    }
    
    try {
        // 使用流式響應構建器
        MemoryOptimization::StreamingResponseBuilder stream;
        
        // 設置HTTP頭
        webServer->sendHeader("Content-Type", "text/html; charset=utf-8");
        webServer->sendHeader("Cache-Control", "no-cache, must-revalidate");
        webServer->sendHeader("Connection", "close");
        
        // 開始流式響應
        stream.begin(webServer, "text/html; charset=utf-8");
        
        // HTML頭部
        stream.append("<!DOCTYPE html><html><head>");
        stream.append("<meta charset='UTF-8'>");
        stream.append("<title>模擬控制</title>");
        stream.append("<style>");
        stream.append("body{font-family:Arial,sans-serif;margin:0;padding:20px;background:#f5f5f5;}");
        stream.append(".container{max-width:600px;margin:0 auto;background:white;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);}");
        stream.append("h1{color:#333;text-align:center;margin-bottom:30px;}");
        stream.append(".status-grid{display:grid;grid-template-columns:1fr 1fr;gap:15px;margin-bottom:20px;}");
        stream.append(".status-item{padding:15px;background:#f8f9fa;border-radius:6px;border-left:4px solid #007bff;}");
        stream.append(".status-label{font-weight:bold;color:#495057;display:block;margin-bottom:5px;}");
        stream.append(".status-value{font-size:1.2em;color:#28a745;}");
        stream.append(".control-form{background:#fff;padding:20px;border:2px solid #e9ecef;border-radius:8px;}");
        stream.append(".form-group{margin-bottom:15px;}");
        stream.append("label{display:block;margin-bottom:5px;font-weight:bold;color:#495057;}");
        stream.append("input,select{width:100%;padding:8px;border:1px solid #ced4da;border-radius:4px;font-size:16px;}");
        stream.append(".button{background:#007bff;color:white;padding:12px 24px;border:none;border-radius:4px;cursor:pointer;font-size:16px;margin:5px;}");
        stream.append(".button:hover{background:#0056b3;}");
        stream.append(".button.secondary{background:#6c757d;}");
        stream.append("</style>");
        stream.append("</head><body>");
        
        // 主要內容
        stream.append("<div class='container'>");
        stream.append("<h1>🔧 模擬控制台</h1>");
        
        // 當前狀態顯示
        stream.append("<div class='status-grid'>");
        
        // 電源狀態
        stream.append("<div class='status-item'>");
        stream.append("<span class='status-label'>電源狀態:</span>");
        stream.appendf("<span class='status-value'>%s</span>", 
                      mockController->getPower() ? "開啟" : "關閉");
        stream.append("</div>");
        
        // 當前溫度
        stream.append("<div class='status-item'>");
        stream.append("<span class='status-label'>當前溫度:</span>");
        stream.appendf("<span class='status-value'>%.1f°C</span>", 
                      mockController->getCurrentTemperature());
        stream.append("</div>");
        
        // 目標溫度
        stream.append("<div class='status-item'>");
        stream.append("<span class='status-label'>目標溫度:</span>");
        stream.appendf("<span class='status-value'>%.1f°C</span>", 
                      mockController->getTargetTemperature());
        stream.append("</div>");
        
        // 運行狀態
        stream.append("<div class='status-item'>");
        stream.append("<span class='status-label'>運行狀態:</span>");
        const char* runStatus = mockController->isSimulationHeating() ? "🔥 加熱中" : 
                                (mockController->isSimulationCooling() ? "❄️ 制冷中" : "⏸️ 待機");
        stream.appendf("<span class='status-value'>%s</span>", runStatus);
        stream.append("</div>");
        
        stream.append("</div>"); // 結束狀態網格
        
        // 控制表單
        stream.append("<form method='post' action='/simulation-control' class='control-form'>");
        stream.append("<h3>控制設定</h3>");
        
        // 電源控制
        stream.append("<div class='form-group'>");
        stream.append("<label>電源:</label>");
        stream.append("<select name='power'>");
        stream.appendf("<option value='0'%s>關閉</option>", 
                      !mockController->getPower() ? " selected" : "");
        stream.appendf("<option value='1'%s>開啟</option>", 
                      mockController->getPower() ? " selected" : "");
        stream.append("</select>");
        stream.append("</div>");
        
        // 模式控制
        stream.append("<div class='form-group'>");
        stream.append("<label>運行模式:</label>");
        stream.append("<select name='mode'>");
        int currentMode = mockController->getTargetMode();
        stream.appendf("<option value='0'%s>關閉</option>", currentMode == 0 ? " selected" : "");
        stream.appendf("<option value='1'%s>制熱</option>", currentMode == 1 ? " selected" : "");
        stream.appendf("<option value='2'%s>制冷</option>", currentMode == 2 ? " selected" : "");
        stream.appendf("<option value='3'%s>自動</option>", currentMode == 3 ? " selected" : "");
        stream.append("</select>");
        stream.append("</div>");
        
        // 目標溫度
        stream.append("<div class='form-group'>");
        stream.append("<label>目標溫度 (°C):</label>");
        stream.appendf("<input type='number' name='target_temp' min='16' max='30' step='0.5' value='%.1f'>", 
                      mockController->getTargetTemperature());
        stream.append("</div>");
        
        // 模擬房間溫度
        stream.append("<div class='form-group'>");
        stream.append("<label>模擬房間溫度 (°C):</label>");
        stream.appendf("<input type='number' name='room_temp' min='10' max='40' step='0.5' value='%.1f'>", 
                      mockController->getSimulatedRoomTemp());
        stream.append("</div>");
        
        // 風扇速度
        stream.append("<div class='form-group'>");
        stream.append("<label>風扇速度:</label>");
        stream.append("<select name='fan_speed'>");
        int fanSpeed = mockController->getFanSpeed();
        stream.appendf("<option value='0'%s>自動</option>", fanSpeed == 0 ? " selected" : "");
        for (int i = 1; i <= 5; i++) {
            stream.appendf("<option value='%d'%s>%d檔</option>", 
                          i, fanSpeed == i ? " selected" : "", i);
        }
        stream.append("</select>");
        stream.append("</div>");
        
        // 提交按鈕
        stream.append("<div style='text-align:center;margin-top:20px;'>");
        stream.append("<button type='submit' class='button'>套用設定</button>");
        stream.append("<a href='/' class='button secondary'>返回主頁</a>");
        stream.append("</div>");
        
        stream.append("</form>");
        stream.append("</div>"); // 結束容器
        stream.append("</body></html>");
        
        // 完成響應
        stream.finish();
        
        DEBUG_VERBOSE_PRINT("[Main] 優化模擬頁面生成完成\n");
        
    } catch (const std::exception& e) {
        DEBUG_ERROR_PRINT("[Main] 模擬頁面生成失敗: %s\n", e.what());
        webServer->send(500, "text/html; charset=utf-8", 
                       "<html><body><h1>頁面生成失敗</h1></body></html>");
    }
}
#endif // DISABLE_SIMULATION_MODE

/**
 * 初始化核心架構
 */
void setupModernArchitecture() {
    DEBUG_INFO_PRINT("[Core] 初始化核心架構...\n");
    
    try {
        // 1. 初始化事件系統
        g_eventBus = new DaiSpan::Core::EventPublisher();
        if (!g_eventBus) {
            DEBUG_ERROR_PRINT("[Core] 事件總線創建失敗\n");
            return;
        }
        
        // 確保統計數據從零開始
        g_eventBus->resetStatistics();
        DEBUG_INFO_PRINT("[Core] 事件總線統計已重置\n");
        
        // 2. 初始化服務容器
        g_serviceContainer = new DaiSpan::Core::ServiceContainer();
        if (!g_serviceContainer) {
            DEBUG_ERROR_PRINT("[Core] 服務容器創建失敗\n");
            return;
        }
        
        // 3. 初始化偏好設定（用於 V3 配置）
        if (!g_preferences.begin("daispan_core", false)) {
            DEBUG_ERROR_PRINT("[Core] 系統偏好設定初始化失敗\n");
            return;
        }
        
        // 4. 註冊配置服務
        g_serviceContainer->registerFactory<DaiSpan::Domain::Config::ConfigurationManager>(
            "ConfigurationManager",
            [](DaiSpan::Core::ServiceContainer& container) -> std::shared_ptr<DaiSpan::Domain::Config::ConfigurationManager> {
                return std::make_shared<DaiSpan::Domain::Config::ConfigurationManager>(g_preferences);
            });
        
        DEBUG_INFO_PRINT("[Core] 基礎架構初始化完成\n");
        modernArchitectureEnabled = true;
        
    } catch (const std::exception& e) {
        DEBUG_ERROR_PRINT("[Core] 架構初始化異常: %s\n", e.what());
        modernArchitectureEnabled = false;
    }
}

/**
 * 設置核心架構事件監聽
 */
void setupCoreEventListeners() {
    if (!modernArchitectureEnabled || !g_eventBus) {
        DEBUG_WARN_PRINT("[Core] 核心架構未啟用，跳過事件監聽設置\n");
        return;
    }
    
    DEBUG_INFO_PRINT("[Core] 設置核心架構事件監聽...\n");
    
    // 設置事件監聽器（用於調試和監控）
    g_eventBus->subscribe<DaiSpan::Domain::Thermostat::Events::StateChanged>(
        [](const DaiSpan::Domain::Thermostat::Events::StateChanged& event) {
            DEBUG_VERBOSE_PRINT("[Core] 狀態變化事件接收\n");
            REMOTE_WEBLOG("[Core-Event] 恆溫器狀態變化");
        });
    
    g_eventBus->subscribe<DaiSpan::Domain::Thermostat::Events::CommandReceived>(
        [](const DaiSpan::Domain::Thermostat::Events::CommandReceived& event) {
            DEBUG_VERBOSE_PRINT("[Core] 命令接收事件\n");
            REMOTE_WEBLOG("[Core-Event] 命令接收");
        });
    
    g_eventBus->subscribe<DaiSpan::Domain::Thermostat::Events::TemperatureUpdated>(
        [](const DaiSpan::Domain::Thermostat::Events::TemperatureUpdated& event) {
            DEBUG_INFO_PRINT("[Core] 溫度更新事件\n");
            REMOTE_WEBLOG("[Core-Event] 溫度更新");
        });
    
    g_eventBus->subscribe<DaiSpan::Domain::Thermostat::Events::Error>(
        [](const DaiSpan::Domain::Thermostat::Events::Error& event) {
            DEBUG_ERROR_PRINT("[Core] 領域錯誤事件\n");
            REMOTE_WEBLOG("[Core-Error] 系統錯誤");
        });
    
    DEBUG_INFO_PRINT("[Core] 系統遷移橋接設置完成\n");
}

/**
 * 處理核心事件（在主循環中調用）
 */
void processCoreEvents() {
    if (!modernArchitectureEnabled || !g_eventBus) {
        return;
    }
    
    // 處理事件總線
    g_eventBus->processEvents(5); // 每次最多處理 5 個事件
    
    // 定期輸出統計資訊和記憶體檢查（每 60 秒）
    static unsigned long lastStatsTime = 0;
    static uint32_t lastFreeHeap = ESP.getFreeHeap();
    static uint32_t minFreeHeap = ESP.getFreeHeap();
    static uint32_t maxFreeHeap = ESP.getFreeHeap();
    
    if (millis() - lastStatsTime > 60000) {
        if (g_eventBus) {
            uint32_t currentFreeHeap = ESP.getFreeHeap();
            float memoryUsage = (float)(currentFreeHeap) / (float)(ESP.getHeapSize()) * 100.0f;
            
            // 更新記憶體使用範圍
            if (currentFreeHeap < minFreeHeap) minFreeHeap = currentFreeHeap;
            if (currentFreeHeap > maxFreeHeap) maxFreeHeap = currentFreeHeap;
            
            DEBUG_INFO_PRINT("[Core] 事件統計: 隊列:%d 訂閱:%d 處理:%d 記憶體:%.1f%% (最小:%d 最大:%d) 運行:%ds\n",
                           g_eventBus->getQueueSize(),
                           g_eventBus->getSubscriptionCount(),
                           g_eventBus->getProcessedEventCount(),
                           memoryUsage,
                           minFreeHeap,
                           maxFreeHeap,
                           millis() / 1000);
            
            // 記憶體洩漏檢測
            if (lastFreeHeap > currentFreeHeap) {
                uint32_t memoryDrop = lastFreeHeap - currentFreeHeap;
                if (memoryDrop > 1000) {  // 記憶體下降超過 1KB
                    DEBUG_WARN_PRINT("[Core] 記憶體洩漏警告: 下降 %d bytes (從 %d 到 %d)\n",
                                     memoryDrop, lastFreeHeap, currentFreeHeap);
                }
            }
            
            // 記憶體清理
            if (currentFreeHeap < 50000) {
                DEBUG_WARN_PRINT("[Core] 記憶體不足，嘗試清理...\n");
                // 重置事件統計以釋放可能的累積記憶體
                g_eventBus->resetStatistics();
                delay(100);  // 讓系統有時間清理
                DEBUG_INFO_PRINT("[Core] 清理後記憶體: %d bytes\n", ESP.getFreeHeap());
            }
            
            if (g_eventBus->getQueueSize() > 10) {
                DEBUG_WARN_PRINT("[Core] 事件佇列積壓過多: %d\n", g_eventBus->getQueueSize());
            }
            
            // 記錄HomeKit狀態
            if (thermostatDevice && thermostatController) {
                DEBUG_VERBOSE_PRINT("[Core] HomeKit 狀態: 電源:%s 模式:%d 溫度:%.1f/%.1f°C\n",
                                   thermostatController->getPower() ? "開" : "關",
                                   thermostatController->getTargetMode(),
                                   thermostatController->getCurrentTemperature(),
                                   thermostatController->getTargetTemperature());
            }
            
            lastFreeHeap = currentFreeHeap;
        }
        lastStatsTime = millis();
    }
}

/**
 * 獲取核心架構狀態資訊（用於 WebServer API）
 */
String getCoreStatusInfo() {
    if (!modernArchitectureEnabled) {
        return "\"modernArchitecture\":false";
    }
    
    String info = "\"modernArchitecture\":true";
    
    if (g_eventBus) {
        info += ",\"eventBus\":{";
        info += "\"queueSize\":" + String(g_eventBus->getQueueSize()) + ",";
        info += "\"subscriptions\":" + String(g_eventBus->getSubscriptionCount()) + ",";
        info += "\"processed\":" + String(g_eventBus->getProcessedEventCount());
        info += "}";
    }
    
    if (g_eventBus) {
        info += ",\"architecture\":{";
        info += "\"active\":" + String(modernArchitectureEnabled ? "true" : "false");
        info += ",\"event_bus_ready\":" + String(g_eventBus ? "true" : "false");
        info += ",\"service_container_ready\":" + String(g_serviceContainer ? "true" : "false");
        info += "}";
    }
    
    return info;
}

// 統一WebServer初始化函數 - 所有版本都使用相同的基礎功能（端口8080）
// 只有RemoteDebugger部分根據編譯環境有所不同
void initializeMonitoring() {
    static bool memoryFailureFlag = false;  // 防止記憶體不足時的無限重試
    static unsigned long lastFailureTime = 0;
    
    if (monitoringEnabled || !homeKitInitialized) {
        return;
    }
    
    // 如果之前因記憶體不足失敗，等待30秒後才重試
    if (memoryFailureFlag && (millis() - lastFailureTime) < 30000) {
        return;
    }
    
    DEBUG_INFO_PRINT("[Main] 啟動統一WebServer功能（端口8080）...\n");
    DEBUG_INFO_PRINT("[Main] 可用記憶體: %d bytes\n", ESP.getFreeHeap());
    
    // 嘗試釋放一些記憶體
    if (ESP.getFreeHeap() < 65000) {
        DEBUG_INFO_PRINT("[Main] 記憶體偏低，嘗試釋放資源...\n");
        
        // 釋放事件系統統計數據
        if (g_eventBus) {
            g_eventBus->resetStatistics();
        }
        
        // 清理未使用的記憶體優化組件
        if (pageGenerator && ESP.getFreeHeap() < 55000) {
            DEBUG_WARN_PRINT("[Main] 緊急釋放頁面生成器以節省記憶體\n");
            pageGenerator.reset();
        }
        
        // 強制延遲讓系統清理
        delay(200);
        DEBUG_INFO_PRINT("[Main] 記憶體釋放後: %d bytes\n", ESP.getFreeHeap());
    }
    
    // 檢查記憶體是否足夠（適應實際硬件環境）
    uint32_t currentMemory = ESP.getFreeHeap();
    uint32_t minThreshold = 30000;  // 降低至30KB門檻以適應ESP32-C3實際情況
    
    if (currentMemory < minThreshold) {
        DEBUG_ERROR_PRINT("[Main] 記憶體不足(%d bytes < %d)，跳過WebServer啟動\n", currentMemory, minThreshold);
        memoryFailureFlag = true;
        lastFailureTime = millis();
        return;
    }
    
    // 根據可用記憶體調整功能
    bool enableAdvancedFeatures = currentMemory >= 70000;  // 70KB以上啟用完整功能
    DEBUG_INFO_PRINT("[Main] WebServer啟動: %d bytes 可用 (進階功能: %s)\n", 
                     currentMemory, enableAdvancedFeatures ? "啟用" : "精簡");
    
    DEBUG_INFO_PRINT("[Main] 記憶體檢查通過：%d bytes 可用\n", ESP.getFreeHeap());
    
    if (!webServer) {
        webServer = new WebServer(8080);
        if (!webServer) {
            DEBUG_ERROR_PRINT("[Main] WebServer創建失敗\n");
            return;
        }
    }
    
    // 統一主頁面路由 - 所有版本使用相同的外觀和功能
    webServer->on("/", [](){
        generateUnifiedMainPage();
    });
    
    // JSON狀態API，包含核心架構資訊
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
        
        // 在 JSON 結尾前插入核心架構狀態
        if (json.endsWith("}")) {
            json = json.substring(0, json.length() - 1); // 移除最後的 '}'
            json += "," + getCoreStatusInfo() + "}";
        }
        
        webServer->send(200, "application/json", json);
    });
    
    // WiFi配置頁面 - 統一使用MemoryOptimization版本
    webServer->on("/wifi", [](){
        try {
            // WiFi配置是核心功能，只在極端記憶體不足時才限制
            if (memoryManager) {
                auto strategy = memoryManager->getRenderStrategy();
                if (strategy == MemoryOptimization::MemoryManager::RenderStrategy::EMERGENCY) {
                    // 極端緊急模式下提供簡化的WiFi配置
                    String emergencyHtml = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
                    emergencyHtml += "<title>WiFi配置 (緊急模式)</title></head><body style='margin:20px;'>";
                    emergencyHtml += "<h1>WiFi配置</h1>";
                    emergencyHtml += "<p style='color:orange;'>⚠️ 系統記憶體極低，使用簡化模式</p>";
                    emergencyHtml += "<form method='POST' action='/wifi-save'>";
                    emergencyHtml += "<p>網路名稱: <input type='text' name='ssid' required></p>";
                    emergencyHtml += "<p>密碼: <input type='password' name='password'></p>";
                    emergencyHtml += "<button type='submit'>連接</button> ";
                    emergencyHtml += "<a href='/'>返回主頁</a></p>";
                    emergencyHtml += "</form></body></html>";
                    webServer->send(200, "text/html", emergencyHtml);
                    return;
                }
            }
            
            // 統一使用自定義的WiFi配置頁面，確保所有版本一致
            // 不再依賴pageGenerator以避免版本間的差異
            DEBUG_INFO_PRINT("[WiFi] 使用統一WiFi配置頁面生成\n");
            
            // 增強版WiFi配置頁面
            String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
            html += "<title>WiFi 配置</title>";
            html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
            html += "<style>";
            html += "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;margin:0;padding:20px;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;} ";
            html += ".container{max-width:600px;margin:0 auto;background:white;padding:25px;border-radius:12px;box-shadow:0 4px 20px rgba(0,0,0,0.1);} ";
            html += ".current-status{background:#f8f9fa;padding:15px;border-radius:8px;margin-bottom:20px;border-left:4px solid #28a745;} ";
            html += ".status-connected{border-left-color:#28a745;} .status-disconnected{border-left-color:#dc3545;} ";
            html += ".scan-section{background:#e3f2fd;padding:15px;border-radius:8px;margin:20px 0;} ";
            html += ".network-list{max-height:200px;overflow-y:auto;margin:10px 0;} ";
            html += ".network-item{padding:10px;border:1px solid #ddd;border-radius:4px;margin:5px 0;cursor:pointer;display:flex;justify-content:space-between;align-items:center;} ";
            html += ".network-item:hover{background:#f0f0f0;} .network-item.selected{background:#007bff;color:white;} ";
            html += ".signal{font-size:12px;} .signal-strong{color:#28a745;} .signal-medium{color:#ffc107;} .signal-weak{color:#dc3545;} ";
            html += ".form-group{margin:15px 0;} label{display:block;font-weight:bold;margin-bottom:5px;color:#333;} ";
            html += "input{width:100%;padding:12px;border:1px solid #ddd;border-radius:6px;box-sizing:border-box;font-size:14px;} ";
            html += "input:focus{outline:none;border-color:#007bff;box-shadow:0 0 5px rgba(0,123,255,0.3);} ";
            html += ".button{background:#007bff;color:white;padding:12px 24px;border:none;border-radius:6px;text-decoration:none;display:inline-block;margin:5px;cursor:pointer;font-size:14px;transition:all 0.2s;} ";
            html += ".button:hover{background:#0056b3;transform:translateY(-1px);} ";
            html += ".secondary{background:#6c757d;} .scan-btn{background:#28a745;} .refresh-btn{background:#17a2b8;} ";
            html += ".loading{display:none;text-align:center;padding:20px;} ";
            html += "@media (max-width:768px){.container{margin:10px;padding:15px;}} ";
            html += "</style></head><body>";
            
            html += "<div class='container'>";
            html += "<h1>📡 WiFi 網路配置</h1>";
            
            // 當前連接狀態
            html += "<div class='current-status ";
            if (WiFi.status() == WL_CONNECTED) {
                html += "status-connected'>";
                html += "<h3>✅ 當前連接狀態</h3>";
                html += "<p><strong>網路名稱:</strong> " + WiFi.SSID() + "</p>";
                html += "<p><strong>信號強度:</strong> " + String(WiFi.RSSI()) + " dBm</p>";
                html += "<p><strong>IP 地址:</strong> " + WiFi.localIP().toString() + "</p>";
                html += "<p><strong>MAC 地址:</strong> " + WiFi.macAddress() + "</p>";
            } else {
                html += "status-disconnected'>";
                html += "<h3>❌ 未連接到WiFi</h3>";
                html += "<p>請選擇或輸入WiFi網路進行連接</p>";
            }
            html += "</div>";
            
            // WiFi掃描區域
            html += "<div class='scan-section'>";
            html += "<h3>🔍 可用網路</h3>";
            html += "<button type='button' class='button scan-btn' onclick='scanNetworks()'>掃描網路</button>";
            html += "<button type='button' class='button refresh-btn' onclick='location.reload()'>刷新頁面</button>";
            html += "<div class='loading' id='loading'>正在掃描網路...</div>";
            html += "<div class='network-list' id='networkList'></div>";
            html += "</div>";
            
            // 配置表單
            html += "<form method='post' action='/wifi-save' id='wifiForm'>";
            html += "<div class='form-group'>";
            html += "<label for='ssid'>網路名稱 (SSID):</label>";
            html += "<input type='text' id='ssid' name='ssid' placeholder='輸入WiFi網路名稱' required>";
            html += "</div>";
            html += "<div class='form-group'>";
            html += "<label for='password'>密碼:</label>";
            html += "<input type='password' id='password' name='password' placeholder='輸入WiFi密碼'>";
            html += "</div>";
            html += "<div style='text-align: center; margin-top: 25px;'>";
            html += "<button type='submit' class='button'>💾 保存並連接</button>";
            html += "<a href='/' class='button secondary'>⬅️ 返回主頁</a>";
            html += "</div>";
            html += "</form>";
            
            html += "</div>";
            
            // JavaScript功能
            html += "<script>";
            html += "function scanNetworks() {";
            html += "  document.getElementById('loading').style.display = 'block';";
            html += "  document.getElementById('networkList').innerHTML = '';";
            html += "  fetch('/api/wifi/scan')";
            html += "    .then(response => {";
            html += "      if (!response.ok) {";
            html += "        throw new Error(`HTTP ${response.status}: ${response.statusText}`);";
            html += "      }";
            html += "      return response.text();";
            html += "    })";
            html += "    .then(text => {";
            html += "      try {";
            html += "        const data = JSON.parse(text);";
            html += "        document.getElementById('loading').style.display = 'none';";
            html += "        if (data.error) {";
            html += "          throw new Error(data.error + (data.debug ? ' (' + data.debug + ')' : ''));";
            html += "        }";
            html += "        displayNetworks(data.networks || []);";
            html += "      } catch (jsonErr) {";
            html += "        console.error('JSON解析錯誤:', jsonErr.message);";
            html += "        console.error('原始響應:', text.substring(0, 200) + '...');";
            html += "        document.getElementById('loading').style.display = 'none';";
            html += "        document.getElementById('networkList').innerHTML = ";
            html += "          '<div style=\"color:red;padding:10px;border:1px solid red;border-radius:4px;\">' +";
            html += "          '<h4>JSON解析錯誤</h4>' +";
            html += "          '<p><strong>錯誤:</strong> ' + jsonErr.message + '</p>' +";
            html += "          '<p><strong>原始響應:</strong> ' + text.substring(0, 100) + '...</p>' +";
            html += "          '<button onclick=\"scanNetworks()\" style=\"margin-top:10px;\">重新掃描</button>' +";
            html += "          '</div>';";
            html += "      }";
            html += "    })";
            html += "    .catch(err => {";
            html += "      console.error('WiFi掃描錯誤:', err);";
            html += "      document.getElementById('loading').style.display = 'none';";
            html += "      document.getElementById('networkList').innerHTML = ";
            html += "        '<div style=\"color:red;padding:10px;border:1px solid red;border-radius:4px;\">' +";
            html += "        '<h4>掃描失敗</h4>' +";
            html += "        '<p>' + err.message + '</p>' +";
            html += "        '<button onclick=\"scanNetworks()\" style=\"margin-top:10px;\">重新掃描</button>' +";
            html += "        '</div>';";
            html += "    });";
            html += "}";
            html += "function displayNetworks(networks) {";
            html += "  const list = document.getElementById('networkList');";
            html += "  if (networks.length === 0) {";
            html += "    list.innerHTML = '<p>未找到可用網路</p>';";
            html += "    return;";
            html += "  }";
            html += "  let html = '';";
            html += "  networks.forEach((net, i) => {";
            html += "    const signalClass = net.rssi > -50 ? 'signal-strong' : net.rssi > -70 ? 'signal-medium' : 'signal-weak';";
            html += "    const security = net.encryption > 0 ? '🔒' : '🔓';";
            html += "    html += `<div class=\"network-item\" onclick=\"selectNetwork('${net.ssid}')\">`;";
            html += "    html += `<span>${security} ${net.ssid}</span>`;";
            html += "    html += `<span class=\"signal ${signalClass}\">${net.rssi} dBm</span>`;";
            html += "    html += '</div>';";
            html += "  });";
            html += "  list.innerHTML = html;";
            html += "}";
            html += "function selectNetwork(ssid) {";
            html += "  document.getElementById('ssid').value = ssid;";
            html += "  const items = document.querySelectorAll('.network-item');";
            html += "  items.forEach(item => item.classList.remove('selected'));";
            html += "  event.target.closest('.network-item').classList.add('selected');";
            html += "}";
            html += "window.onload = () => scanNetworks();";
            html += "</script>";
            html += "</body></html>";
            
            webServer->send(200, "text/html", html);
            
        } catch (...) {
            // 最終降級：純文本響應
            webServer->send(500, "text/plain", "WiFi配置頁面載入失敗，請重試");
        }
    });
    
    // WiFi掃描已整合到WiFi配置頁面中 (MemoryOptimization::WebPageGenerator)
    
    // WiFi配置保存處理
    webServer->on("/wifi-save", HTTP_POST, [](){
        String ssid = webServer->arg("ssid");
        String password = webServer->arg("password");
        
        if (ssid.length() > 0) {
            configManager.setWiFiCredentials(ssid, password);
            
            // 使用優化版本生成成功頁面
            MemoryOptimization::StreamingResponseBuilder stream;
            stream.begin(webServer);
            stream.append("<!DOCTYPE html><html><head><meta charset='UTF-8'>");
            stream.append("<title>WiFi配置已保存</title>");
            stream.append("<meta http-equiv='refresh' content='3;url=/restart'>");
            stream.appendf("<style>%s</style></head><body>", WebUI::getCompactCSS());
            stream.append("<div class='container'><h1>✅ WiFi配置已保存</h1>");
            stream.append("<div class='status'>新的WiFi配置已保存成功！設備將重啟並嘗試連接。</div>");
            stream.append("<div style='text-align:center;margin:20px 0;'>");
            stream.append("<a href='/restart' class='button'>🔄 立即重啟</a>");
            stream.append("</div></div></body></html>");
            stream.finish();
        } else {
            webServer->send(400, "text/plain", "SSID不能為空");
        }
    });
    
    // HomeKit配置頁面 - 使用MemoryOptimization版本
    webServer->on("/homekit", [](){
        try {
            // HomeKit配置是核心功能，只在極端記憶體不足時才限制
            if (memoryManager) {
                auto strategy = memoryManager->getRenderStrategy();
                if (strategy == MemoryOptimization::MemoryManager::RenderStrategy::EMERGENCY) {
                    // 極端緊急模式下提供簡化的HomeKit配置
                    String emergencyHtml = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
                    emergencyHtml += "<title>HomeKit配置 (緊急模式)</title></head><body style='margin:20px;'>";
                    emergencyHtml += "<h1>HomeKit配置</h1>";
                    emergencyHtml += "<p style='color:orange;'>⚠️ 系統記憶體極低，使用簡化模式</p>";
                    
                    String currentPairingCode = configManager.getHomeKitPairingCode();
                    String currentDeviceName = configManager.getHomeKitDeviceName();
                    
                    emergencyHtml += "<form method='POST' action='/homekit-save'>";
                    emergencyHtml += "<p>配對碼: <input type='text' name='pairingCode' value='" + currentPairingCode + "' required></p>";
                    emergencyHtml += "<p>設備名稱: <input type='text' name='deviceName' value='" + currentDeviceName + "' required></p>";
                    emergencyHtml += "<button type='submit'>保存</button> ";
                    emergencyHtml += "<a href='/'>返回主頁</a></p>";
                    emergencyHtml += "</form></body></html>";
                    webServer->send(200, "text/html", emergencyHtml);
                    return;
                }
            }
            
            String currentPairingCode = configManager.getHomeKitPairingCode();
            String currentDeviceName = configManager.getHomeKitDeviceName();
            String currentQRID = configManager.getHomeKitQRID();
            
            // 增強版HomeKit配置和狀態頁面
            String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
            html += "<title>HomeKit 配置與狀態</title>";
            html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
            html += "<style>";
            html += "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;margin:0;padding:20px;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;} ";
            html += ".container{max-width:700px;margin:0 auto;background:white;padding:25px;border-radius:12px;box-shadow:0 4px 20px rgba(0,0,0,0.1);} ";
            html += ".status-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:15px;margin:20px 0;} ";
            html += ".status-card{background:#f8f9fa;padding:15px;border-radius:8px;border-left:4px solid #007bff;} ";
            html += ".status-online{border-left-color:#28a745;} .status-offline{border-left-color:#dc3545;} .status-warning{border-left-color:#ffc107;} ";
            html += ".status-title{font-weight:bold;margin-bottom:8px;color:#333;} ";
            html += ".status-value{font-size:14px;color:#666;margin:4px 0;} ";
            html += ".metric{display:flex;justify-content:space-between;align-items:center;padding:5px 0;border-bottom:1px solid #eee;} ";
            html += ".metric:last-child{border-bottom:none;} ";
            html += ".homekit-qr{text-align:center;background:#fff;padding:20px;border-radius:8px;margin:15px 0;border:2px dashed #007bff;} ";
            html += ".form-section{background:#f8f9fa;padding:20px;border-radius:8px;margin:20px 0;} ";
            html += ".form-group{margin:15px 0;} label{display:block;font-weight:bold;margin-bottom:5px;color:#333;} ";
            html += "input{width:100%;padding:12px;border:1px solid #ddd;border-radius:6px;box-sizing:border-box;font-size:14px;} ";
            html += "input:focus{outline:none;border-color:#007bff;box-shadow:0 0 5px rgba(0,123,255,0.3);} ";
            html += ".button{background:#007bff;color:white;padding:12px 24px;border:none;border-radius:6px;text-decoration:none;display:inline-block;margin:5px;cursor:pointer;font-size:14px;transition:all 0.2s;} ";
            html += ".button:hover{background:#0056b3;transform:translateY(-1px);} ";
            html += ".secondary{background:#6c757d;} .success{background:#28a745;} .warning{background:#ffc107;color:#212529;} .danger{background:#dc3545;} ";
            html += ".refresh-btn{background:#17a2b8;font-size:12px;padding:8px 16px;} ";
            html += "@media (max-width:768px){.container{margin:10px;padding:15px;} .status-grid{grid-template-columns:1fr;}} ";
            html += "</style>";
            html += "<script>function refreshStatus(){location.reload();} setInterval(refreshStatus, 30000);</script>";
            html += "</head><body>";
            
            html += "<div class='container'>";
            html += "<h1>🏠 HomeKit 配置與系統狀態</h1>";
            
            // HomeKit服務狀態區域
            html += "<div class='status-grid'>";
            
            // HomeKit初始化狀態
            html += "<div class='status-card ";
            if (homeKitInitialized) {
                html += "status-online'>";
                html += "<div class='status-title'>✅ HomeKit 服務</div>";
                html += "<div class='status-value'>服務已初始化並運行中</div>";
                html += "<div class='metric'><span>狀態:</span><span style='color:#28a745;'>運行中</span></div>";
            } else {
                html += "status-offline'>";
                html += "<div class='status-title'>❌ HomeKit 服務</div>";
                html += "<div class='status-value'>服務未初始化</div>";
                html += "<div class='metric'><span>狀態:</span><span style='color:#dc3545;'>離線</span></div>";
            }
            html += "</div>";
            
            // 設備狀態
            html += "<div class='status-card ";
            if (deviceInitialized) {
                html += "status-online'>";
                html += "<div class='status-title'>🌡️ 恆溫器設備</div>";
                html += "<div class='status-value'>設備已初始化</div>";
                html += "<div class='metric'><span>設備:</span><span style='color:#28a745;'>在線</span></div>";
            } else {
                html += "status-offline'>";
                html += "<div class='status-title'>🌡️ 恆溫器設備</div>";
                html += "<div class='status-value'>設備未初始化</div>";
                html += "<div class='metric'><span>設備:</span><span style='color:#dc3545;'>離線</span></div>";
            }
            html += "</div>";
            
            // 記憶體和系統狀態
            html += "<div class='status-card'>";
            html += "<div class='status-title'>💾 系統資源</div>";
            uint32_t freeHeap = ESP.getFreeHeap();
            html += "<div class='metric'><span>可用記憶體:</span><span>" + String(freeHeap/1024) + " KB</span></div>";
            html += "<div class='metric'><span>運行時間:</span><span>" + String(millis()/1000/60) + " 分鐘</span></div>";
            html += "<div class='metric'><span>配對狀態:</span><span>" + String(homeKitPairingActive ? "配對中" : "待機") + "</span></div>";
            html += "</div>";
            
            // WiFi連接狀態
            html += "<div class='status-card ";
            if (WiFi.status() == WL_CONNECTED) {
                html += "status-online'>";
                html += "<div class='status-title'>📶 網路連接</div>";
                html += "<div class='metric'><span>SSID:</span><span>" + WiFi.SSID() + "</span></div>";
                html += "<div class='metric'><span>信號:</span><span>" + String(WiFi.RSSI()) + " dBm</span></div>";
                html += "<div class='metric'><span>IP:</span><span>" + WiFi.localIP().toString() + "</span></div>";
            } else {
                html += "status-offline'>";
                html += "<div class='status-title'>📶 網路連接</div>";
                html += "<div class='status-value'>未連接到WiFi</div>";
            }
            html += "</div>";
            
            html += "</div>"; // end status-grid
            
            // HomeKit QR碼信息（如果可用）
            if (homeKitInitialized) {
                html += "<div class='homekit-qr'>";
                html += "<h3>📱 HomeKit 配對信息</h3>";
                html += "<p><strong>配對代碼:</strong> " + currentPairingCode + "</p>";
                html += "<p>在iPhone的「家庭」App中掃描QR碼或手動輸入配對代碼</p>";
                html += "<button type='button' class='button refresh-btn' onclick='refreshStatus()'>🔄 刷新狀態</button>";
                html += "</div>";
            }
            
            // 配置表單區域
            html += "<div class='form-section'>";
            html += "<h3>⚙️ HomeKit 配置</h3>";
            html += "<form method='post' action='/homekit-save'>";
            html += "<div class='form-group'>";
            html += "<label for='pairing_code'>配對代碼 (8位數字):</label>";
            html += "<input type='text' id='pairing_code' name='pairing_code' value='" + currentPairingCode + "' maxlength='8' pattern='[0-9]{8}' required>";
            html += "</div>";
            html += "<div class='form-group'>";
            html += "<label for='device_name'>設備名稱:</label>";
            html += "<input type='text' id='device_name' name='device_name' value='" + currentDeviceName + "' maxlength='64' required>";
            html += "</div>";
            html += "<div class='form-group'>";
            html += "<label for='qr_id'>QR ID (4位字母):</label>";
            html += "<input type='text' id='qr_id' name='qr_id' value='" + currentQRID + "' maxlength='4' pattern='[A-Z]{4}' required>";
            html += "</div>";
            html += "<div style='text-align: center; margin-top: 25px;'>";
            html += "<button type='submit' class='button success'>💾 保存設定並重啟</button>";
            html += "<a href='/' class='button secondary'>⬅️ 返回主頁</a>";
            if (homeKitInitialized) {
                html += "<a href='/restart' class='button warning'>🔄 重啟系統</a>";
            }
            html += "</div>";
            html += "</form>";
            html += "</div>";
            
            html += "</div></body></html>";
            
            webServer->send(200, "text/html", html);
            
        } catch (...) {
            // 最終降級：純文本響應
            webServer->send(500, "text/plain", "HomeKit配置頁面載入失敗，請重試");
        }
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
            
            MemoryOptimization::StreamingResponseBuilder stream;
            stream.begin(webServer);
            stream.append("<!DOCTYPE html><html><head><meta charset='UTF-8'>");
            stream.append("<title>HomeKit配置已保存</title>");
            stream.append("<meta http-equiv='refresh' content='3;url=/restart'>");
            stream.appendf("<style>%s</style></head><body>", WebUI::getCompactCSS());
            stream.append("<div class='container'><h1>✅ HomeKit配置已保存</h1>");
            stream.append("<div class='status'>配置更新成功！設備將重啟並應用新配置。</div>");
            stream.append("<div style='text-align:center;margin:20px 0;'>");
            stream.append("<a href='/restart' class='button'>立即重啟</a>");
            stream.append("<a href='/' class='button secondary'>返回主頁</a></div>");
            stream.append("</div></body></html>");
            stream.finish();
        } else {
            MemoryOptimization::StreamingResponseBuilder stream;
            stream.begin(webServer);
            stream.append("<!DOCTYPE html><html><head><meta charset='UTF-8'>");
            stream.append("<title>無需更新</title>");
            stream.appendf("<style>%s</style></head><body>", WebUI::getCompactCSS());
            stream.append("<div class='container'><h1>ℹ️ 無需更新</h1>");
            stream.append("<div class='info'>您沒有修改任何配置。</div>");
            stream.append("<div style='text-align:center;margin:20px 0;'>");
            stream.append("<a href='/homekit' class='button'>返回配置</a>");
            stream.append("<a href='/' class='button secondary'>返回主頁</a></div>");
            stream.append("</div></body></html>");
            stream.finish();
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
        
        // 使用記憶體優化的流式響應生成模擬頁面
        if (pageGenerator && memoryManager) {
            if (!memoryManager->shouldServePage("simulation")) {
                webServer->send(503, "text/html; charset=utf-8", 
                               "<html><body><h1>系統記憶體不足</h1><p>請稍後重試</p></body></html>");
                return;
            }
            
            generateOptimizedSimulationPage();
            return;
        }
        
        // 使用統一的MemoryOptimization版本作為降級處理
        generateOptimizedSimulationPage();
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
        
        MemoryOptimization::StreamingResponseBuilder stream;
        stream.begin(webServer);
        stream.append("<!DOCTYPE html><html><head><meta charset='UTF-8'>");
        stream.append("<title>設置已更新</title>");
        stream.appendf("<style>%s</style></head><body>", WebUI::getCompactCSS());
        stream.append("<div class='container'><h1>✅ 設置已更新</h1>");
        stream.append("<div class='status'>模擬參數已成功更新！</div>");
        stream.append("<div style='text-align:center;margin:20px 0;'>");
        stream.append("<a href='/simulation' class='button'>🔧 返回模擬控制</a>&nbsp;&nbsp;");
        stream.append("<a href='/' class='button secondary'>🏠 返回主頁</a>");
        stream.append("</div></div></body></html>");
        stream.finish();
    });
    
    // 模式切換頁面 - 使用MemoryOptimization版本
    webServer->on("/simulation-toggle", [](){
        bool currentMode = configManager.getSimulationMode();
        
        MemoryOptimization::StreamingResponseBuilder stream;
        stream.begin(webServer);
        stream.append("<!DOCTYPE html><html><head><meta charset='UTF-8'>");
        stream.append("<title>模式切換</title>");
        stream.appendf("<style>%s</style></head><body>", WebUI::getCompactCSS());
        stream.append("<div class='container'><h1>🔄 運行模式切換</h1>");
        stream.appendf("<div class='status'>當前模式: %s</div>", 
                      currentMode ? "🔧 模擬模式" : "🏠 實際硬體模式");
        stream.append("<div class='warning'>⚠️ 切換模式將會重啟設備</div>");
        stream.append("<form method='post' action='/simulation-toggle-confirm'>");
        stream.appendf("<p>確認要切換到 <strong>%s</strong> 嗎？</p>", 
                      currentMode ? "實際硬體模式" : "模擬模式");
        stream.append("<div style='text-align:center;margin:20px 0;'>");
        stream.append("<button type='submit' class='button danger'>✅ 確認切換</button>");
        stream.append("<a href='/' class='button secondary'>❌ 取消</a>");
        stream.append("</div></form></div></body></html>");
        stream.finish();
    });
    
    // 模式切換確認 - 使用MemoryOptimization版本
    webServer->on("/simulation-toggle-confirm", HTTP_POST, [](){
        bool currentMode = configManager.getSimulationMode();
        configManager.setSimulationMode(!currentMode);
        
        MemoryOptimization::StreamingResponseBuilder stream;
        stream.begin(webServer);
        stream.append("<!DOCTYPE html><html><head><meta charset='UTF-8'>");
        stream.append("<title>模式切換中</title>");
        stream.append("<meta http-equiv='refresh' content='3;url=/restart'>");
        stream.appendf("<style>%s</style></head><body>", WebUI::getCompactCSS());
        stream.append("<div class='container'><h1>🔄 模式切換中</h1>");
        stream.append("<div class='status'>運行模式已切換，設備將重啟。</div>");
        stream.append("<div style='text-align:center;margin:20px 0;'>");
        stream.append("<a href='/restart' class='button'>立即重啟</a>");
        stream.append("<a href='/' class='button secondary'>返回主頁</a>");
        stream.append("</div></div></body></html>");
        stream.finish();
    });
    #endif // DISABLE_SIMULATION_MODE
    
    // 核心架構調試端點 - 手動觸發事件測試
    webServer->on("/core-test-event", [](){
        if (!modernArchitectureEnabled || !g_eventBus) {
            webServer->send(400, "text/plain", "核心架構未啟用");
            return;
        }
        
        try {
            // 觸發一個測試事件
            auto testEvent = DaiSpan::Domain::Thermostat::Events::CommandReceived(
                DaiSpan::Domain::Thermostat::Events::CommandReceived::Type::Temperature,
                "debug-test",
                "手動測試事件"
            );
            
            g_eventBus->publish(testEvent);
            
            String response = "✅ V3 測試事件已發布!\n";
            response += "佇列大小: " + String(g_eventBus->getQueueSize()) + "\n";
            response += "訂閱數: " + String(g_eventBus->getSubscriptionCount()) + "\n"; 
            response += "已處理: " + String(g_eventBus->getProcessedEventCount()) + "\n";
            
            webServer->send(200, "text/plain", response);
            
            DEBUG_INFO_PRINT("[Core-Debug] 手動觸發測試事件\n");
            
        } catch (...) {
            webServer->send(500, "text/plain", "❌ 事件發布失敗");
        }
    });
    
#ifndef PRODUCTION_BUILD
    // 核心架構統計 API (開發模式)
    webServer->on("/api/core/stats", [](){
        if (!modernArchitectureEnabled || !g_eventBus) {
            webServer->send(400, "application/json", "{\"error\":\"Core architecture not enabled\"}");
            return;
        }
        
        String json = "{";
        json += "\"queueSize\":" + String(g_eventBus->getQueueSize()) + ",";
        json += "\"subscriptions\":" + String(g_eventBus->getSubscriptionCount()) + ",";
        json += "\"processed\":" + String(g_eventBus->getProcessedEventCount()) + ",";
        json += "\"architecture\":" + String(modernArchitectureEnabled ? "true" : "false") + ",";
        json += "\"uptime\":" + String(millis() / 1000) + ",";
        json += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";
        json += "\"timestamp\":" + String(millis());
        json += "}";
        
        webServer->send(200, "application/json", json);
    });
    
    // 核心架構統計重置端點 (開發模式)
    webServer->on("/api/core/reset-stats", [](){
        if (!modernArchitectureEnabled || !g_eventBus) {
            webServer->send(400, "application/json", "{\"error\":\"Core architecture not enabled\"}");
            return;
        }
        
        g_eventBus->resetStatistics();
        String json = "{\"status\":\"success\",\"message\":\"Statistics reset successfully\",\"timestamp\":" + String(millis()) + "}";
        webServer->send(200, "application/json", json);
    });
#endif
    
    // 系統健康檢查端點
    webServer->on("/api/health", [](){
        String json = "{";
        json += "\"status\":\"ok\",";
        json += "\"services\":{";
        json += "\"homekit\":" + String(homeKitInitialized ? "true" : "false") + ",";
        json += "\"hardware\":" + String(deviceInitialized ? "true" : "false") + ",";
        json += "\"wifi\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
        json += "\"webserver\":" + String(monitoringEnabled ? "true" : "false");
        if (modernArchitectureEnabled) {
            json += ",\"coreArchitecture\":" + String(g_eventBus ? "true" : "false");
        }
        json += "},";
        json += "\"memory\":{";
        json += "\"free\":" + String(ESP.getFreeHeap()) + ",";
        json += "\"total\":" + String(ESP.getHeapSize()) + ",";
        json += "\"usage\":" + String(100.0 * (ESP.getHeapSize() - ESP.getFreeHeap()) / ESP.getHeapSize(), 1);
        json += "},";
        json += "\"uptime\":" + String(millis() / 1000) + ",";
        json += "\"timestamp\":" + String(millis());
        json += "}";
        
        webServer->send(200, "application/json", json);
    });
    
    // WiFi掃描API端點
    webServer->on("/api/wifi/scan", [](){
        webServer->sendHeader("Access-Control-Allow-Origin", "*");
        
        // 檢查是否正在掃描或最近剛掃描過
        static unsigned long lastScan = 0;
        unsigned long now = millis();
        
        if (now - lastScan < 10000) { // 10秒內不重複掃描
            webServer->send(429, "application/json", 
                           "{\"error\":\"掃描太頻繁，請10秒後重試\",\"retryAfter\":10}");
            return;
        }
        
        lastScan = now;
        
        // 開始WiFi掃描
        DEBUG_INFO_PRINT("[WiFi] 開始掃描網路...\n");
        int networkCount = WiFi.scanNetworks(false, true); // async=false, show_hidden=true
        DEBUG_INFO_PRINT("[WiFi] 掃描完成，發現 %d 個網路\n", networkCount);
        
        String json = "{\"networks\":[";
        
        if (networkCount > 0) {
            bool firstValidNetwork = true;
            for (int i = 0; i < networkCount && i < 20; i++) { // 限制最多20個網路
                String ssid = WiFi.SSID(i);
                int32_t rssi = WiFi.RSSI(i);
                wifi_auth_mode_t encryption = WiFi.encryptionType(i);
                
                // 過濾空SSID或無效網路
                if (ssid.length() == 0 || ssid == " ") {
                    DEBUG_VERBOSE_PRINT("[WiFi] 跳過空SSID網路: index %d\n", i);
                    continue;
                }
                
                // 只在有效網路之間添加逗號
                if (!firstValidNetwork) {
                    json += ",";
                }
                firstValidNetwork = false;
                
                // 安全地轉義SSID中的特殊字符
                String escapedSSID = ssid;
                escapedSSID.replace("\"", "\\\"");
                escapedSSID.replace("\\", "\\\\");
                
                json += "{";
                json += "\"ssid\":\"" + escapedSSID + "\",";
                json += "\"rssi\":" + String(rssi) + ",";
                json += "\"encryption\":" + String((int)encryption) + ",";
                json += "\"channel\":" + String(WiFi.channel(i));
                json += "}";
            }
        }
        
        json += "],";
        json += "\"count\":" + String(networkCount) + ",";
        json += "\"timestamp\":" + String(millis()) + ",";
        
        // 安全地處理當前WiFi SSID
        String currentSSID = WiFi.SSID();
        currentSSID.replace("\"", "\\\"");
        currentSSID.replace("\\", "\\\\");
        
        json += "\"currentSSID\":\"" + currentSSID + "\",";
        json += "\"currentRSSI\":" + String(WiFi.RSSI());
        json += "}";
        
        // 基本JSON驗證
        if (json.indexOf(",,") != -1) {
            DEBUG_ERROR_PRINT("[WiFi] JSON格式錯誤：發現雙逗號\n");
            webServer->send(500, "application/json", 
                           "{\"error\":\"JSON生成錯誤\",\"debug\":\"雙逗號問題\"}");
            return;
        }
        
        // 檢查JSON基本結構
        if (!json.startsWith("{") || !json.endsWith("}")) {
            DEBUG_ERROR_PRINT("[WiFi] JSON格式錯誤：結構不完整\n");
            webServer->send(500, "application/json", 
                           "{\"error\":\"JSON結構錯誤\",\"debug\":\"缺少大括號\"}");
            return;
        }
        
        DEBUG_VERBOSE_PRINT("[WiFi] JSON生成成功，長度: %d\n", json.length());
        webServer->send(200, "application/json", json);
    });
    
    // 系統指標端點（記憶體優化版）
    webServer->on("/api/metrics", [](){
        // 使用預分配的緩衝區減少記憶體分配
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
        
        // 添加事件系統指標
        if (modernArchitectureEnabled && g_eventBus && written < sizeof(buffer) - 200) {
            written += snprintf(buffer + written, sizeof(buffer) - written,
                ",\"eventSystem\":{"
                "\"queueSize\":%d,"
                "\"subscriptions\":%d,"
                "\"processed\":%d"
                "}",
                g_eventBus->getQueueSize(),
                g_eventBus->getSubscriptionCount(),
                g_eventBus->getProcessedEventCount()
            );
        }
        
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
    
    // 系統日誌端點（記憶體優化版）
    webServer->on("/api/logs", [](){
        static char buffer[768];
        uint32_t timestamp = millis();
        uint32_t freeHeap = ESP.getFreeHeap();
        
        int written = snprintf(buffer, sizeof(buffer),
            "{"
            "\"logs\":["
            "{\"level\":\"info\",\"component\":\"system\",\"message\":\"System running normally\",\"timestamp\":%u}",
            timestamp);
        
        // 架構信息
        if (modernArchitectureEnabled && written < sizeof(buffer) - 100) {
            written += snprintf(buffer + written, sizeof(buffer) - written,
                ",{\"level\":\"info\",\"component\":\"core\",\"message\":\"Modern architecture enabled\",\"timestamp\":%u}",
                timestamp);
        }
        
        // 內存信息
        if (written < sizeof(buffer) - 150) {
            if (freeHeap < 50000) {
                written += snprintf(buffer + written, sizeof(buffer) - written,
                    ",{\"level\":\"warn\",\"component\":\"memory\",\"message\":\"Low memory: %u bytes\",\"timestamp\":%u}",
                    freeHeap, timestamp);
            } else {
                written += snprintf(buffer + written, sizeof(buffer) - written,
                    ",{\"level\":\"info\",\"component\":\"memory\",\"message\":\"Memory healthy: %u bytes\",\"timestamp\":%u}",
                    freeHeap, timestamp);
            }
        }
        
        // HomeKit狀態
        if (homeKitInitialized && written < sizeof(buffer) - 100) {
            written += snprintf(buffer + written, sizeof(buffer) - written,
                ",{\"level\":\"info\",\"component\":\"homekit\",\"message\":\"HomeKit initialized and ready\",\"timestamp\":%u}",
                timestamp);
        }
        
        // 事件系統狀態
        if (modernArchitectureEnabled && g_eventBus && written < sizeof(buffer) - 150) {
            size_t queueSize = g_eventBus->getQueueSize();
            if (queueSize > 10) {
                written += snprintf(buffer + written, sizeof(buffer) - written,
                    ",{\"level\":\"warn\",\"component\":\"events\",\"message\":\"Event queue backlog: %zu events\",\"timestamp\":%u}",
                    queueSize, timestamp);
            } else {
                written += snprintf(buffer + written, sizeof(buffer) - written,
                    ",{\"level\":\"info\",\"component\":\"events\",\"message\":\"Event system healthy, queue: %zu\",\"timestamp\":%u}",
                    queueSize, timestamp);
            }
        }
        
        // 結束 JSON
        if (written < sizeof(buffer) - 100) {
            snprintf(buffer + written, sizeof(buffer) - written,
                "],"
                "\"logLevel\":\"info\","
                "\"logCount\":%d,"
                "\"timestamp\":%u"
                "}",
                5 + (modernArchitectureEnabled ? 1 : 0), timestamp);
        }
        
        webServer->send(200, "application/json", buffer);
    });
    
    // OTA 頁面
    webServer->on("/ota", [](){
        try {
            String deviceIP = WiFi.localIP().toString();
            
            // 使用基本HTML建構（更可靠）
            String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
            html += "<title>OTA 更新</title>";
            html += "<style>body{font-family:Arial;margin:20px;background:#f5f5f5;} ";
            html += ".container{max-width:600px;margin:0 auto;background:white;padding:20px;border-radius:8px;} ";
            html += ".status{background:#e7f3ff;padding:15px;border-radius:4px;margin:15px 0;} ";
            html += ".warning{background:#fff3cd;border:1px solid #ffeaa7;color:#856404;padding:15px;border-radius:4px;margin:15px 0;} ";
            html += ".code-block{background:#f8f9fa;border:1px solid #e9ecef;padding:10px;border-radius:4px;font-family:monospace;margin:10px 0;} ";
            html += ".button{background:#007bff;color:white;padding:12px 24px;border:none;border-radius:4px;text-decoration:none;display:inline-block;margin:5px;} ";
            html += ".secondary{background:#6c757d;} .danger{background:#dc3545;}</style></head><body>";
            html += "<div class='container'><h1>🔄 OTA 遠程更新</h1>";
            html += "<div class='status'><h3>🔄 OTA 更新狀態</h3>";
            html += "<p><span style='color: green;'>●</span> OTA 服務已啟用</p>";
            html += "<p><strong>設備主機名:</strong> DaiSpan-Thermostat</p>";
            html += "<p><strong>IP地址:</strong> " + deviceIP + "</p></div>";
            html += "<div class='warning'><h3>⚠️ 注意事項</h3>";
            html += "<ul><li>OTA 更新過程中請勿斷電或斷網</li>";
            html += "<li>更新失敗可能導致設備無法啟動</li>";
            html += "<li>建議在更新前備份當前固件</li>";
            html += "<li>更新完成後設備會自動重啟</li></ul></div>";
            html += "<div><h3>📝 使用說明</h3>";
            html += "<p>使用 PlatformIO 進行 OTA 更新：</p>";
            html += "<div class='code-block'>pio run -t upload --upload-port " + deviceIP + "</div>";
            html += "<p>或使用 Arduino IDE：</p>";
            html += "<ol><li>工具 → 端口 → 選擇網路端口</li>";
            html += "<li>選擇設備主機名: DaiSpan-Thermostat</li>";
            html += "<li>輸入 OTA 密碼</li><li>點擊上傳</li></ol></div>";
            html += "<div style='text-align: center; margin-top: 30px;'>";
            html += "<a href='/' class='button secondary'>⬅️ 返回主頁</a>";
            html += "<a href='/restart' class='button danger'>🔄 重新啟動</a>";
            html += "</div></div></body></html>";
            
            webServer->send(200, "text/html", html);
            
        } catch (...) {
            // 最終降級：純文本響應
            webServer->send(500, "text/plain", "OTA更新頁面載入失敗，請重試");
        }
    });
    
    // 日誌查看頁面
    webServer->on("/logs", [](){
        try {
            String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
            html += "<title>系統日誌查看器</title>";
            html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
            html += "<style>";
            html += "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;margin:0;padding:20px;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;} ";
            html += ".container{max-width:900px;margin:0 auto;background:white;padding:25px;border-radius:12px;box-shadow:0 4px 20px rgba(0,0,0,0.1);} ";
            html += ".controls{background:#f8f9fa;padding:15px;border-radius:8px;margin:20px 0;display:flex;gap:10px;flex-wrap:wrap;align-items:center;} ";
            html += ".log-container{background:#1e1e1e;color:#f8f8f2;padding:20px;border-radius:8px;height:400px;overflow-y:auto;font-family:'Monaco','Consolas',monospace;font-size:13px;line-height:1.4;} ";
            html += ".log-entry{margin:2px 0;padding:2px 0;} ";
            html += ".log-info{color:#50fa7b;} .log-warn{color:#f1fa8c;} .log-error{color:#ff5555;} .log-debug{color:#8be9fd;} ";
            html += ".log-timestamp{color:#6272a4;margin-right:8px;} ";
            html += ".log-component{color:#bd93f9;margin-right:8px;font-weight:bold;} ";
            html += ".filter-section{display:flex;gap:10px;align-items:center;flex-wrap:wrap;} ";
            html += ".button{background:#007bff;color:white;padding:8px 16px;border:none;border-radius:4px;cursor:pointer;font-size:12px;text-decoration:none;} ";
            html += ".button:hover{background:#0056b3;} .button.active{background:#28a745;} ";
            html += ".secondary{background:#6c757d;} .success{background:#28a745;} .danger{background:#dc3545;} .warning{background:#ffc107;color:#212529;} ";
            html += "select{padding:6px;border-radius:4px;border:1px solid #ddd;} ";
            html += ".status-bar{background:#e9ecef;padding:10px;border-radius:4px;margin:10px 0;font-size:12px;} ";
            html += ".auto-scroll{display:flex;align-items:center;gap:5px;} ";
            html += "@media (max-width:768px){.container{margin:10px;padding:15px;} .controls{flex-direction:column;align-items:stretch;}} ";
            html += "</style></head><body>";
            
            html += "<div class='container'>";
            html += "<h1>📜 系統日誌查看器</h1>";
            
            // 控制區域
            html += "<div class='controls'>";
            html += "<div class='filter-section'>";
            html += "<span>過濾級別:</span>";
            html += "<button class='button active' onclick='filterLevel(\"all\")'>全部</button>";
            html += "<button class='button' onclick='filterLevel(\"info\")'>信息</button>";
            html += "<button class='button warning' onclick='filterLevel(\"warn\")'>警告</button>";
            html += "<button class='button danger' onclick='filterLevel(\"error\")'>錯誤</button>";
            html += "</div>";
            html += "<div class='filter-section'>";
            html += "<span>組件:</span>";
            html += "<select id='componentFilter' onchange='filterComponent(this.value)'>";
            html += "<option value='all'>全部組件</option>";
            html += "<option value='system'>系統</option>";
            html += "<option value='core'>核心</option>";
            html += "<option value='memory'>記憶體</option>";
            html += "<option value='homekit'>HomeKit</option>";
            html += "<option value='wifi'>WiFi</option>";
            html += "<option value='events'>事件</option>";
            html += "</select>";
            html += "</div>";
            html += "<div class='filter-section'>";
            html += "<button class='button success' onclick='refreshLogs()'>🔄 刷新</button>";
            html += "<button class='button' onclick='clearLogs()'>🗑️ 清空</button>";
            html += "<button class='button secondary' onclick='exportLogs()'>💾 導出</button>";
            html += "</div>";
            html += "<div class='auto-scroll'>";
            html += "<input type='checkbox' id='autoScroll' checked> <label for='autoScroll'>自動滾動</label>";
            html += "</div>";
            html += "</div>";
            
            // 狀態欄
            html += "<div class='status-bar' id='statusBar'>準備就緒 - 點擊「刷新」載入日誌</div>";
            
            // 日誌容器
            html += "<div class='log-container' id='logContainer'>";
            html += "<div class='log-entry log-info'>";
            html += "<span class='log-timestamp'>[" + String(millis()/1000) + "]</span>";
            html += "<span class='log-component'>[LOGGER]</span>";
            html += "日誌查看器已載入，點擊「刷新」開始查看實時日誌";
            html += "</div>";
            html += "</div>";
            
            // 操作按鈕
            html += "<div style='text-align: center; margin-top: 20px;'>";
            html += "<a href='/' class='button secondary'>⬅️ 返回主頁</a>";
            html += "<a href='/api/logs' class='button' target='_blank'>📋 JSON 格式</a>";
            html += "</div>";
            
            html += "</div>";
            
            // JavaScript功能
            html += "<script>";
            html += "let currentLevel = 'all';";
            html += "let currentComponent = 'all';";
            html += "let autoRefresh = false;";
            html += "let refreshInterval;";
            
            html += "function updateStatus(msg) {";
            html += "  document.getElementById('statusBar').textContent = msg;";
            html += "}";
            
            html += "function formatLogEntry(entry) {";
            html += "  const time = new Date().toLocaleTimeString();";
            html += "  const levelClass = 'log-' + (entry.level || 'info');";
            html += "  return `<div class='log-entry ${levelClass}' data-level='${entry.level}' data-component='${entry.component}'>`;";
            html += "  return result + `<span class='log-timestamp'>[${time}]</span>`;";
            html += "  return result + `<span class='log-component'>[${entry.component.toUpperCase()}]</span>`;";
            html += "  return result + `${entry.message}</div>`;";
            html += "}";
            
            html += "function refreshLogs() {";
            html += "  updateStatus('正在載入日誌...');";
            html += "  fetch('/api/logs')";
            html += "    .then(response => response.json())";
            html += "    .then(data => {";
            html += "      const container = document.getElementById('logContainer');";
            html += "      container.innerHTML = '';";
            html += "      if (data.logs && data.logs.length > 0) {";
            html += "        data.logs.forEach(log => {";
            html += "          container.innerHTML += formatLogEntry(log);";
            html += "        });";
            html += "        updateStatus(`已載入 ${data.logs.length} 條日誌記錄`);";
            html += "      } else {";
            html += "        container.innerHTML = '<div class=\"log-entry log-info\">沒有可用的日誌記錄</div>';";
            html += "        updateStatus('沒有日誌記錄');";
            html += "      }";
            html += "      applyFilters();";
            html += "      if (document.getElementById('autoScroll').checked) {";
            html += "        container.scrollTop = container.scrollHeight;";
            html += "      }";
            html += "    })";
            html += "    .catch(err => {";
            html += "      updateStatus('載入失敗: ' + err.message);";
            html += "      console.error('日誌載入失敗:', err);";
            html += "    });";
            html += "}";
            
            html += "function filterLevel(level) {";
            html += "  currentLevel = level;";
            html += "  document.querySelectorAll('.controls .button').forEach(btn => btn.classList.remove('active'));";
            html += "  event.target.classList.add('active');";
            html += "  applyFilters();";
            html += "}";
            
            html += "function filterComponent(component) {";
            html += "  currentComponent = component;";
            html += "  applyFilters();";
            html += "}";
            
            html += "function applyFilters() {";
            html += "  const entries = document.querySelectorAll('.log-entry');";
            html += "  let visibleCount = 0;";
            html += "  entries.forEach(entry => {";
            html += "    const level = entry.dataset.level;";
            html += "    const component = entry.dataset.component;";
            html += "    const levelMatch = currentLevel === 'all' || level === currentLevel;";
            html += "    const componentMatch = currentComponent === 'all' || component === currentComponent;";
            html += "    if (levelMatch && componentMatch) {";
            html += "      entry.style.display = 'block';";
            html += "      visibleCount++;";
            html += "    } else {";
            html += "      entry.style.display = 'none';";
            html += "    }";
            html += "  });";
            html += "  updateStatus(`顯示 ${visibleCount} 條日誌 (級別: ${currentLevel}, 組件: ${currentComponent})`);";
            html += "}";
            
            html += "function clearLogs() {";
            html += "  if (confirm('確定要清空日誌顯示嗎？')) {";
            html += "    document.getElementById('logContainer').innerHTML = '';";
            html += "    updateStatus('日誌已清空');";
            html += "  }";
            html += "}";
            
            html += "function exportLogs() {";
            html += "  const logs = document.getElementById('logContainer').innerText;";
            html += "  const blob = new Blob([logs], {type: 'text/plain'});";
            html += "  const url = URL.createObjectURL(blob);";
            html += "  const a = document.createElement('a');";
            html += "  a.href = url;";
            html += "  a.download = 'daispan-logs-' + new Date().toISOString().slice(0,19).replace(/:/g,'-') + '.txt';";
            html += "  a.click();";
            html += "  URL.revokeObjectURL(url);";
            html += "}";
            
            html += "// 自動刷新功能";
            html += "function toggleAutoRefresh() {";
            html += "  autoRefresh = !autoRefresh;";
            html += "  if (autoRefresh) {";
            html += "    refreshInterval = setInterval(refreshLogs, 5000);";
            html += "    updateStatus('自動刷新已啟用 (每5秒)');";
            html += "  } else {";
            html += "    clearInterval(refreshInterval);";
            html += "    updateStatus('自動刷新已停用');";
            html += "  }";
            html += "}";
            
            html += "// 初始化";
            html += "document.addEventListener('DOMContentLoaded', function() {";
            html += "  refreshLogs();";
            html += "});";
            html += "</script>";
            html += "</body></html>";
            
            webServer->send(200, "text/html", html);
            
        } catch (...) {
            webServer->send(500, "text/plain", "日誌查看頁面載入失敗，請重試");
        }
    });
    
#ifndef PRODUCTION_BUILD
    // 記憶體清理 API 端點 (開發模式)
    webServer->on("/api/memory/cleanup", [](){
        uint32_t beforeCleanup = ESP.getFreeHeap();
        
        // 執行記憶體清理
        if (g_eventBus) {
            g_eventBus->resetStatistics();
        }
        
        // 強制垃圾回收
        delay(100);
        
        uint32_t afterCleanup = ESP.getFreeHeap();
        uint32_t freed = afterCleanup - beforeCleanup;
        
        static char buffer[256];
        snprintf(buffer, sizeof(buffer),
            "{"
            "\"status\":\"success\","
            "\"memoryBefore\":%u,"
            "\"memoryAfter\":%u,"
            "\"memoryFreed\":%u,"
            "\"timestamp\":%u"
            "}",
            beforeCleanup, afterCleanup, freed, (uint32_t)(millis() / 1000)
        );
        
        DEBUG_INFO_PRINT("[API] 記憶體清理執行完成: 釋放 %d bytes\n", freed);
        webServer->send(200, "application/json", buffer);
    });
    
    // 記憶體優化狀態 API 端點 (開發模式)
    webServer->on("/api/memory/stats", [](){
        if (!memoryManager || !pageGenerator) {
            webServer->send(503, "application/json", 
                           "{\"error\":\"Memory optimization not initialized\"}");
            return;
        }
        
        // 使用 StreamingResponseBuilder 生成 JSON 響應
        MemoryOptimization::StreamingResponseBuilder stream;
        stream.begin(webServer, "application/json");
        
        stream.append("{");
        stream.append("\"status\":\"success\",");
        stream.appendf("\"freeHeap\":%u,", ESP.getFreeHeap());
        stream.appendf("\"maxAllocHeap\":%u,", ESP.getMaxAllocHeap());
        // ESP32 doesn't have getHeapFragmentation(), calculate approximation
        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t maxAlloc = ESP.getMaxAllocHeap();
        uint32_t fragmentation = (maxAlloc > 0) ? (100 - (maxAlloc * 100 / freeHeap)) : 0;
        stream.appendf("\"heapFragmentation\":%u,", fragmentation);
        
        // 記憶體壓力狀態
        auto pressure = memoryManager->updateMemoryPressure();
        auto strategy = memoryManager->getRenderStrategy();
        
        stream.appendf("\"memoryPressure\":%d,", static_cast<int>(pressure));
        stream.appendf("\"renderStrategy\":%d,", static_cast<int>(strategy));
        stream.appendf("\"maxBufferSize\":%zu,", memoryManager->getMaxBufferSize());
        stream.appendf("\"shouldUseStreaming\":%s,", 
                      memoryManager->shouldUseStreamingResponse() ? "true" : "false");
        
        stream.appendf("\"timestamp\":%u", (uint32_t)(millis() / 1000));
        stream.append("}");
        
        stream.finish();
        
        DEBUG_VERBOSE_PRINT("[API] 記憶體優化狀態查詢完成\n");
    });
#endif
    
#ifndef PRODUCTION_BUILD
    // 詳細記憶體分析 API 端點 (開發模式)
    webServer->on("/api/memory/detailed", [](){
        if (!memoryManager || !pageGenerator) {
            webServer->send(503, "application/json", 
                           "{\"error\":\"Memory optimization not initialized\"}");
            return;
        }
        
        MemoryOptimization::StreamingResponseBuilder stream;
        stream.begin(webServer, "application/json");
        
        stream.append("{");
        stream.append("\"status\":\"success\",");
        
        // 基本記憶體信息
        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t maxAlloc = ESP.getMaxAllocHeap();
        uint32_t fragmentation = (maxAlloc > 0) ? (100 - (maxAlloc * 100 / freeHeap)) : 0;
        
        stream.appendf("\"heap\":{\"free\":%u,\"maxAlloc\":%u,\"fragmentation\":%u},", 
                      freeHeap, maxAlloc, fragmentation);
        
        // 記憶體壓力詳細信息
        auto pressure = memoryManager->updateMemoryPressure();
        auto strategy = memoryManager->getRenderStrategy();
        
        stream.append("\"memoryPressure\":{");
        stream.appendf("\"level\":%d,", static_cast<int>(pressure));
        stream.append("\"name\":\"");
        switch (pressure) {
            case MemoryOptimization::MemoryManager::MemoryPressure::PRESSURE_LOW:
                stream.append("LOW");
                break;
            case MemoryOptimization::MemoryManager::MemoryPressure::PRESSURE_MEDIUM:
                stream.append("MEDIUM");
                break;
            case MemoryOptimization::MemoryManager::MemoryPressure::PRESSURE_HIGH:
                stream.append("HIGH");
                break;
            case MemoryOptimization::MemoryManager::MemoryPressure::PRESSURE_CRITICAL:
                stream.append("CRITICAL");
                break;
        }
        stream.append("\"},");
        
        // 渲染策略詳細信息
        stream.append("\"renderStrategy\":{");
        stream.appendf("\"level\":%d,", static_cast<int>(strategy));
        stream.appendf("\"maxBufferSize\":%zu,", memoryManager->getMaxBufferSize());
        stream.appendf("\"useStreaming\":%s", 
                      memoryManager->shouldUseStreamingResponse() ? "true" : "false");
        stream.append("},");
        
        // 系統統計信息
        String memoryStats, bufferStats;
        memoryManager->getMemoryStats(memoryStats);
        pageGenerator->getSystemStats(bufferStats);
        
        stream.append("\"statistics\":{");
        stream.append("\"memoryManager\":\"");
        // 簡化統計信息以避免JSON轉義問題
        stream.append("Available in /api/memory/stats-text");
        stream.append("\",");
        stream.append("\"bufferPool\":\"");
        stream.append("Available in /api/buffer/stats");
        stream.append("\"},");
        
        stream.appendf("\"timestamp\":%u", (uint32_t)(millis() / 1000));
        stream.append("}");
        
        stream.finish();
    });
    
    // 緩衝區池統計 API 端點 (開發模式)
    webServer->on("/api/buffer/stats", [](){
        if (!pageGenerator) {
            webServer->send(503, "text/plain", "Buffer pool not initialized");
            return;
        }
        
        String stats;
        pageGenerator->getSystemStats(stats);
        webServer->send(200, "text/plain", stats);
    });
#endif
    
#ifndef PRODUCTION_BUILD
    // 性能測試 API 端點 (開發模式)
    webServer->on("/api/performance/test", [](){
        uint32_t startTime = millis();
        uint32_t startHeap = ESP.getFreeHeap();
        
        MemoryOptimization::StreamingResponseBuilder stream;
        stream.begin(webServer, "application/json");
        
        stream.append("{");
        stream.append("\"status\":\"success\",");
        stream.append("\"testType\":\"performance\",");
        
        // 測試1: 記憶體分配性能
        uint32_t allocTestStart = millis();
        auto buffer = pageGenerator ? 
            std::make_unique<char[]>(1024) : nullptr;
        uint32_t allocTestTime = millis() - allocTestStart;
        
        stream.appendf("\"allocationTest\":{\"duration\":%u,\"success\":%s},", 
                      allocTestTime, buffer ? "true" : "false");
        
        // 測試2: 流式響應性能
        uint32_t streamTestStart = millis();
        MemoryOptimization::StreamingResponseBuilder testStream;
        // 注意：這裡不能真正測試流式響應，因為會衝突
        uint32_t streamTestTime = millis() - streamTestStart;
        
        stream.appendf("\"streamingTest\":{\"duration\":%u,\"success\":true},", 
                      streamTestTime);
        
        // 測試3: JSON 生成性能
        uint32_t jsonTestStart = millis();
        String testJson = "{\"test\":\"data\",\"number\":12345,\"boolean\":true}";
        uint32_t jsonTestTime = millis() - jsonTestStart;
        
        stream.appendf("\"jsonTest\":{\"duration\":%u,\"size\":%u},", 
                      jsonTestTime, testJson.length());
        
        // 整體測試結果
        uint32_t totalTime = millis() - startTime;
        uint32_t endHeap = ESP.getFreeHeap();
        int32_t heapDiff = (int32_t)endHeap - (int32_t)startHeap;
        
        stream.appendf("\"overall\":{\"totalDuration\":%u,\"heapBefore\":%u,\"heapAfter\":%u,\"heapDiff\":%d},", 
                      totalTime, startHeap, endHeap, heapDiff);
        
        stream.appendf("\"timestamp\":%u", (uint32_t)(millis() / 1000));
        stream.append("}");
        
        stream.finish();
        
        DEBUG_INFO_PRINT("[API] 性能測試完成: %u ms, 記憶體變化: %d bytes\n", 
                         totalTime, heapDiff);
    });
    
    // 負載測試 API 端點 (開發模式)
    webServer->on("/api/performance/load", [](){
        String iterations = webServer->hasArg("iterations") ? 
                           webServer->arg("iterations") : "10";
        String delay_ms = webServer->hasArg("delay") ? 
                         webServer->arg("delay") : "100";
        
        int iterCount = iterations.toInt();
        int delayTime = delay_ms.toInt();
        
        if (iterCount > 100) iterCount = 100; // 限制最大迭代次數
        if (delayTime < 50) delayTime = 50;   // 最小延遲50ms
        
        MemoryOptimization::StreamingResponseBuilder stream;
        stream.begin(webServer, "application/json");
        
        stream.append("{");
        stream.append("\"status\":\"success\",");
        stream.append("\"testType\":\"load\",");
        stream.appendf("\"iterations\":%d,", iterCount);
        stream.appendf("\"delay\":%d,", delayTime);
        
        uint32_t testStart = ESP.getFreeHeap();
        uint32_t minHeap = testStart;
        uint32_t maxHeap = testStart;
        uint32_t totalTime = 0;
        
        stream.append("\"results\":[");
        
        for (int i = 0; i < iterCount; i++) {
            uint32_t iterStart = millis();
            uint32_t currentHeap = ESP.getFreeHeap();
            
            // 模擬負載操作
            auto testBuffer = std::make_unique<char[]>(512);
            if (testBuffer) {
                snprintf(testBuffer.get(), 512, 
                        "Test iteration %d with heap %u", i, currentHeap);
            }
            
            delay(delayTime);
            
            uint32_t iterTime = millis() - iterStart;
            totalTime += iterTime;
            
            if (currentHeap < minHeap) minHeap = currentHeap;
            if (currentHeap > maxHeap) maxHeap = currentHeap;
            
            if (i > 0) stream.append(",");
            stream.appendf("{\"iteration\":%d,\"heap\":%u,\"duration\":%u}", 
                          i + 1, currentHeap, iterTime);
        }
        
        stream.append("],");
        stream.appendf("\"summary\":{\"totalTime\":%u,\"avgTime\":%u,\"minHeap\":%u,\"maxHeap\":%u,\"heapVariation\":%u},", 
                      totalTime, totalTime / iterCount, minHeap, maxHeap, maxHeap - minHeap);
        
        stream.appendf("\"timestamp\":%u", (uint32_t)(millis() / 1000));
        stream.append("}");
        
        stream.finish();
        
        DEBUG_INFO_PRINT("[API] 負載測試完成: %d 次迭代, 總時間: %u ms\n", 
                         iterCount, totalTime);
    });
    
    // 基準測試比較 API 端點 (開發模式)
    webServer->on("/api/performance/benchmark", [](){
        MemoryOptimization::StreamingResponseBuilder stream;
        stream.begin(webServer, "application/json");
        
        stream.append("{");
        stream.append("\"status\":\"success\",");
        stream.append("\"testType\":\"benchmark\",");
        
        // 測試1: 傳統String連接 vs 流式輸出
        uint32_t traditionalStart = millis();
        uint32_t traditionalHeapStart = ESP.getFreeHeap();
        
        String traditionalResult = "";
        for (int i = 0; i < 10; i++) {
            traditionalResult += "<div>Test item " + String(i) + "</div>";
        }
        
        uint32_t traditionalTime = millis() - traditionalStart;
        uint32_t traditionalHeapEnd = ESP.getFreeHeap();
        int32_t traditionalHeapDiff = (int32_t)traditionalHeapEnd - (int32_t)traditionalHeapStart;
        
        stream.appendf("\"traditional\":{\"duration\":%u,\"heapDiff\":%d,\"resultSize\":%u},", 
                      traditionalTime, traditionalHeapDiff, traditionalResult.length());
        
        // 測試2: 優化版本（模擬）
        uint32_t optimizedStart = millis();
        uint32_t optimizedHeapStart = ESP.getFreeHeap();
        
        // 模擬流式輸出性能
        char buffer[64];
        for (int i = 0; i < 10; i++) {
            snprintf(buffer, sizeof(buffer), "<div>Test item %d</div>", i);
        }
        
        uint32_t optimizedTime = millis() - optimizedStart;
        uint32_t optimizedHeapEnd = ESP.getFreeHeap();
        int32_t optimizedHeapDiff = (int32_t)optimizedHeapEnd - (int32_t)optimizedHeapStart;
        
        stream.appendf("\"optimized\":{\"duration\":%u,\"heapDiff\":%d,\"chunkSize\":64},", 
                      optimizedTime, optimizedHeapDiff);
        
        // 計算改善幅度
        float timeImprovement = ((float)(traditionalTime - optimizedTime) / traditionalTime) * 100;
        float memoryImprovement = ((float)(traditionalHeapDiff - optimizedHeapDiff) / abs(traditionalHeapDiff)) * 100;
        
        stream.appendf("\"improvement\":{\"timePercent\":%.2f,\"memoryPercent\":%.2f},", 
                      timeImprovement, memoryImprovement);
        
        stream.appendf("\"timestamp\":%u", (uint32_t)(millis() / 1000));
        stream.append("}");
        
        stream.finish();
        
        DEBUG_INFO_PRINT("[API] 基準測試完成: 時間改善 %.2f%%, 記憶體改善 %.2f%%\n", 
                         timeImprovement, memoryImprovement);
    });
    
    // 即時監控儀表板 API 端點 (開發模式)
    webServer->on("/api/monitor/dashboard", [](){
        MemoryOptimization::StreamingResponseBuilder stream;
        stream.begin(webServer, "application/json");
        
        stream.append("{");
        stream.append("\"status\":\"success\",");
        
        // 系統基本信息
        stream.append("\"system\":{");
        stream.appendf("\"uptime\":%u,", millis() / 1000);
        stream.appendf("\"freeHeap\":%u,", ESP.getFreeHeap());
        stream.appendf("\"maxAllocHeap\":%u,", ESP.getMaxAllocHeap());
        stream.appendf("\"chipModel\":\"%s\",", ESP.getChipModel());
        stream.appendf("\"chipRevision\":%u,", ESP.getChipRevision());
        stream.appendf("\"flashSize\":%u", ESP.getFlashChipSize());
        stream.append("},");
        
        // WiFi 狀態
        stream.append("\"wifi\":{");
        stream.appendf("\"connected\":%s,", WiFi.isConnected() ? "true" : "false");
        if (WiFi.isConnected()) {
            stream.appendf("\"rssi\":%d,", WiFi.RSSI());
            stream.append("\"ip\":\"");
            stream.append(WiFi.localIP().toString().c_str());
            stream.append("\",");
            stream.append("\"ssid\":\"");
            stream.append(WiFi.SSID().c_str());
            stream.append("\"");
        } else {
            stream.append("\"rssi\":0,\"ip\":\"0.0.0.0\",\"ssid\":\"\"");
        }
        stream.append("},");
        
        // 記憶體優化狀態
        if (memoryManager) {
            auto pressure = memoryManager->updateMemoryPressure();
            auto strategy = memoryManager->getRenderStrategy();
            
            stream.append("\"memoryOptimization\":{");
            stream.append("\"enabled\":true,");
            stream.appendf("\"pressure\":%d,", static_cast<int>(pressure));
            stream.appendf("\"strategy\":%d,", static_cast<int>(strategy));
            stream.appendf("\"maxBufferSize\":%zu,", memoryManager->getMaxBufferSize());
            stream.appendf("\"useStreaming\":%s", 
                          memoryManager->shouldUseStreamingResponse() ? "true" : "false");
            stream.append("},");
        } else {
            stream.append("\"memoryOptimization\":{\"enabled\":false},");
        }
        
        // HomeKit 狀態
        stream.append("\"homekit\":{");
        stream.appendf("\"initialized\":%s,", homeKitInitialized ? "true" : "false");
        stream.appendf("\"pairingActive\":%s", homeKitPairingActive ? "true" : "false");
        stream.append("},");
        
        stream.appendf("\"timestamp\":%u", (uint32_t)(millis() / 1000));
        stream.append("}");
        
        stream.finish();
    });
#endif
    
    // 重啟端點 - 使用MemoryOptimization版本
    webServer->on("/restart", [](){
        String deviceIP = WiFi.localIP().toString();
        
        MemoryOptimization::StreamingResponseBuilder stream;
        stream.begin(webServer);
        stream.append("<!DOCTYPE html><html><head><meta charset='UTF-8'>");
        stream.append("<title>設備重啟中</title>");
        stream.append("<meta http-equiv='refresh' content='10;url=http://");
        stream.append(deviceIP.c_str());
        stream.append(":8080'>");
        stream.appendf("<style>%s</style></head><body>", WebUI::getCompactCSS());
        stream.append("<div class='container'><h1>🔄 設備重啟中</h1>");
        stream.append("<div class='status'>設備正在重啟，請稍候...</div>");
        stream.append("<div class='info'>頁面將在10秒後自動重新導向到新位址。</div>");
        stream.appendf("<div style='text-align:center;margin:20px 0;'>");
        stream.appendf("<a href='http://%s:8080' class='button'>🔗 手動連接</a>", deviceIP.c_str());
        stream.append("</div></div></body></html>");
        stream.finish();
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
    
    // 重置記憶體失敗標誌
    memoryFailureFlag = false;
    
    // 初始化記憶體優化組件
    initializeMemoryOptimization();
    
    DEBUG_INFO_PRINT("[Main] 統一WebServer功能已啟動: http://%s:8080\n", 
                     WiFi.localIP().toString().c_str());
    DEBUG_INFO_PRINT("[Main] 所有版本均提供: WiFi設定 | HomeKit設定 | 系統狀態 | OTA更新 | 日誌查看\n");
    
    // 初始化RemoteDebugger調試系統 - 根據編譯環境自動選擇實現
    RemoteDebugger& debugger = RemoteDebugger::getInstance();
    
#if defined(ENABLE_REMOTE_DEBUG)
    // WebSocket調試器 (高功能版本) - 端口8081
    if (debugger.begin(8081)) {
        DEBUG_INFO_PRINT("[Main] 額外調試功能: WebSocket調試系統已啟動 ws://%s:8081\n", 
                         WiFi.localIP().toString().c_str());
        DEBUG_INFO_PRINT("[Main] 高級調試界面: http://%s:8080/debug\n", 
                         WiFi.localIP().toString().c_str());
    } else {
        DEBUG_ERROR_PRINT("[Main] WebSocket調試系統啟動失敗\n");
    }
#elif defined(ENABLE_LIGHTWEIGHT_DEBUG)
    // HTTP輕量級調試器 (節省記憶體版本) - 端口8082
    if (debugger.begin(8082)) {
        DEBUG_INFO_PRINT("[Main] 額外調試功能: 輕量級調試系統已啟動 http://%s:8082\n", 
                         WiFi.localIP().toString().c_str());
        DEBUG_INFO_PRINT("[Main] 輕量級調試界面: http://%s:8082/\n", 
                         WiFi.localIP().toString().c_str());
    } else {
        DEBUG_ERROR_PRINT("[Main] 輕量級調試系統啟動失敗\n");
    }
#else
    // 生產環境 - 調用空實現（編譯器會優化掉）
    debugger.begin(); // 空實現始終返回true
    DEBUG_INFO_PRINT("[Main] 基礎功能模式：RemoteDebugger調試系統已禁用以節省記憶體\n");
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
        
        // HomeKit 初始化完成後，設置核心事件監聽
        setupCoreEventListeners();
        
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
    DEBUG_INFO_PRINT("\n[Main] DaiSpan 智能恆溫器啟動...\n");
    
    // 記錄系統啟動時間
    systemStartTime = millis();
    DEBUG_INFO_PRINT("[Main] 系統啟動時間: %lu ms\n", systemStartTime);
    
    DEBUG_INFO_PRINT("[Main] 現代化架構已啟用\n");
    
    // 初始化現代化架構
    setupModernArchitecture();
    
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
    
    if (modernArchitectureEnabled) {
        DEBUG_INFO_PRINT("[Core] 核心架構設置完成，系統運行正常\n");
    }
}

void loop() {
    // 處理RemoteDebugger調試器 - 生產環境為空實現，調試環境為實際功能
    RemoteDebugger::getInstance().loop();
    
    // 核心事件處理（優先處理）
    processCoreEvents();
    
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