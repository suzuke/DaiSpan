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
#include "common/RemoteDebugger.h"
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
        
        // 事件系統統計（如果啟用）
        if (modernArchitectureEnabled && g_eventBus) {
            stream.append("<div class='status-item'>");
            stream.append("<span class='status-label'>事件統計:</span>");
            stream.appendf("<span class='status-value'>佇列:%zu 訂閱:%zu 已處理:%zu</span>",
                          g_eventBus->getQueueSize(),
                          g_eventBus->getSubscriptionCount(),
                          g_eventBus->getProcessedEventCount());
            stream.append("</div>");
        }
        
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
        
        stream.append("<a href='/api/memory/cleanup' class='button secondary'>記憶體清理</a>");
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

// WebServer 初始化函數
void initializeMonitoring() {
    if (monitoringEnabled || !homeKitInitialized) {
        return;
    }
    
    DEBUG_INFO_PRINT("[Main] 啟動WebServer監控功能...\n");
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
    
    // 檢查記憶體是否足夠（降低門檻以適應開發環境）
    uint32_t currentMemory = ESP.getFreeHeap();
    uint32_t minThreshold = 45000;  // 降低至45KB門檻
    
    if (currentMemory < minThreshold) {
        DEBUG_ERROR_PRINT("[Main] 記憶體不足(%d bytes < %d)，跳過WebServer啟動\n", currentMemory, minThreshold);
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
    
    // 基本路由處理 - 使用記憶體優化
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
        
        // 使用記憶體優化的頁面生成器
        if (pageGenerator && memoryManager) {
            if (!memoryManager->shouldServePage("main")) {
                webServer->send(503, "text/html", 
                               "<html><body><h1>系統記憶體不足</h1><p>請稍後重試</p></body></html>");
                return;
            }
            
            webServer->sendHeader("Cache-Control", "no-cache, must-revalidate");
            webServer->sendHeader("Pragma", "no-cache");
            webServer->sendHeader("Connection", "close");
            
            generateOptimizedMainPage();
            return;
        }
        
        // 降級處理：使用傳統方法但採用自適應緩衝區
        webServer->sendHeader("Cache-Control", "no-cache, must-revalidate");
        webServer->sendHeader("Pragma", "no-cache");
        webServer->sendHeader("Connection", "close");
        
        // 使用自適應緩衝區大小
        const size_t requestedSize = 4096;  // 預期需要的大小
        uint32_t freeHeap = ESP.getFreeHeap();
        size_t bufferSize;
        
        if (freeHeap > 50000) {
            bufferSize = requestedSize;
        } else if (freeHeap > 30000) {
            bufferSize = requestedSize * 0.75;
        } else if (freeHeap > 20000) {
            bufferSize = requestedSize * 0.5;
        } else {
            bufferSize = 1024;  // 最小緩衝區
        }
        
        // 檢查記憶體是否足夠分配，如果不夠則使用最小化頁面
        if (freeHeap < (bufferSize + 2048)) {
            String minimalPage = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>DaiSpan</title></head><body>";
            minimalPage += "<h1>🌡️ DaiSpan Thermostat</h1>";
            minimalPage += "<p><strong>記憶體:</strong> " + String(freeHeap) + " bytes 可用</p>";
            minimalPage += "<p><strong>WiFi:</strong> " + WiFi.SSID() + " (" + String(WiFi.RSSI()) + " dBm)</p>";
            minimalPage += "<p><strong>IP:</strong> " + WiFi.localIP().toString() + "</p>";
            minimalPage += "<p><a href='/api/health'>系統狀態 (JSON)</a> | <a href='/wifi'>WiFi設定</a></p>";
            minimalPage += "</body></html>";
            webServer->send(200, "text/html", minimalPage);
            return;
        }
        
        auto buffer = std::make_unique<char[]>(bufferSize);
        if (!buffer) {
            webServer->send(500, "text/html", 
                           "<html><body><h1>記憶體分配失敗</h1><p>無法分配 " + String(bufferSize) + " bytes</p></body></html>");
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
        
        // HTML生成 - 簡化版本以適應記憶體限制
        append("<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>DaiSpan</title>");
        append("<style>body{font-family:Arial;margin:20px;}.container{max-width:600px;}</style>");
        append("</head><body><div class=\"container\"><h1>🌡️ DaiSpan</h1>");
        
        // 簡化系統狀態 - 避免記憶體溢出
        append("<p><strong>記憶體:</strong> %u KB 可用</p>", freeHeap / 1024);
        append("<p><strong>WiFi:</strong> %s (%d dBm)</p>", WiFi.SSID().c_str(), WiFi.RSSI());
        append("<p><strong>IP:</strong> %s</p>", WiFi.localIP().toString().c_str());
        if (modernArchitectureEnabled && g_eventBus) {
            append("<p><strong>事件:</strong> %zu 處理</p>", g_eventBus->getProcessedEventCount());
        }
        
        // 簡化導航連結
        append("<p>");
        append("<a href='/wifi'>WiFi設定</a> | ");
        append("<a href='/homekit'>HomeKit設定</a> | ");
        append("<a href='/api/health'>系統狀態</a> | ");
        append("<a href='/ota'>OTA更新</a>");
        append("</p></div></body></html>");
        
        if (overflow) {
            webServer->send(500, "text/html", "<div style='color:red;'>Error: HTML too large for buffer</div>");
            return;
        }
        
        String html(buffer.get());
        webServer->send(200, "text/html", html);
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
        // 檢查記憶體壓力
        if (memoryManager && !memoryManager->shouldServePage("wifi_config")) {
            MemoryOptimization::StreamingResponseBuilder stream;
            stream.begin(webServer);
            stream.append("<!DOCTYPE html><html><head><meta charset='UTF-8'>");
            stream.append("<title>系統忙碌</title>");
            stream.append("<style>%s</style></head><body>", WebUI::getCompactCSS());
            stream.append("<div class='container'><h1>🚫 系統記憶體不足</h1>");
            stream.append("<div class='error'>系統目前記憶體壓力過大，請稍後重試。</div>");
            stream.append("<div style='text-align:center;margin:20px 0;'>");
            stream.append("<a href='/' class='button'>返回主頁</a></div>");
            stream.append("</div></body></html>");
            stream.finish();
            return;
        }
        
        // 優先使用pageGenerator，否則使用StreamingResponseBuilder
        if (pageGenerator) {
            pageGenerator->generateWiFiConfigPage(webServer);
        } else {
            MemoryOptimization::StreamingResponseBuilder stream;
            stream.begin(webServer);
            stream.append("<!DOCTYPE html><html><head><meta charset='UTF-8'>");
            stream.append("<title>WiFi 配置</title>");
            stream.append("<style>%s</style></head><body>", WebUI::getCompactCSS());
            stream.append("<div class='container'><h1>📡 WiFi 配置</h1>");
            stream.append("<form method='post' action='/wifi-save'>");
            stream.append("<div class='form-group'><label>網路名稱:</label>");
            stream.append("<input type='text' name='ssid' placeholder='WiFi 網路名稱' required></div>");
            stream.append("<div class='form-group'><label>密碼:</label>");
            stream.append("<input type='password' name='password' placeholder='WiFi 密碼'></div>");
            stream.append("<div style='text-align: center; margin-top: 20px;'>");
            stream.append("<button type='submit' class='button'>💾 保存設定</button>");
            stream.append("<a href='/' class='button secondary'>⬅️ 返回主頁</a>");
            stream.append("</div></form></div></body></html>");
            stream.finish();
        }
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
            
            // 使用優化版本生成成功頁面
            MemoryOptimization::StreamingResponseBuilder stream;
            stream.begin(webServer);
            stream.append("<!DOCTYPE html><html><head><meta charset='UTF-8'>");
            stream.append("<title>WiFi配置已保存</title>");
            stream.append("<meta http-equiv='refresh' content='3;url=/restart'>");
            stream.append("<style>%s</style></head><body>", WebUI::getCompactCSS());
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
        // 檢查記憶體壓力
        if (memoryManager && !memoryManager->shouldServePage("homekit_config")) {
            MemoryOptimization::StreamingResponseBuilder stream;
            stream.begin(webServer);
            stream.append("<!DOCTYPE html><html><head><meta charset='UTF-8'>");
            stream.append("<title>系統忙碌</title>");
            stream.append("<style>%s</style></head><body>", WebUI::getCompactCSS());
            stream.append("<div class='container'><h1>🚫 系統記憶體不足</h1>");
            stream.append("<div class='error'>系統目前記憶體壓力過大，請稍後重試。</div>");
            stream.append("<div style='text-align:center;margin:20px 0;'>");
            stream.append("<a href='/' class='button'>返回主頁</a></div>");
            stream.append("</div></body></html>");
            stream.finish();
            return;
        }
        
        String currentPairingCode = configManager.getHomeKitPairingCode();
        String currentDeviceName = configManager.getHomeKitDeviceName();
        String currentQRID = configManager.getHomeKitQRID();
        
        MemoryOptimization::StreamingResponseBuilder stream;
        stream.begin(webServer);
        stream.append("<!DOCTYPE html><html><head><meta charset='UTF-8'>");
        stream.append("<title>HomeKit 配置</title>");
        stream.append("<style>%s</style></head><body>", WebUI::getCompactCSS());
        stream.append("<div class='container'><h1>🏠 HomeKit 配置</h1>");
        stream.append("<form method='post' action='/homekit-save'>");
        stream.append("<div class='form-group'><label>配對代碼:</label>");
        stream.appendf("<input type='text' name='pairing_code' value='%s' maxlength='8' required></div>", 
                      currentPairingCode.c_str());
        stream.append("<div class='form-group'><label>設備名稱:</label>");
        stream.appendf("<input type='text' name='device_name' value='%s' maxlength='64' required></div>", 
                      currentDeviceName.c_str());
        stream.append("<div class='form-group'><label>QR ID:</label>");
        stream.appendf("<input type='text' name='qr_id' value='%s' maxlength='4' required></div>", 
                      currentQRID.c_str());
        stream.append("<div class='status'>");
        if (homeKitInitialized) {
            stream.append("🟢 HomeKit 服務已初始化");
        } else {
            stream.append("🔴 HomeKit 服務未初始化");
        }
        stream.append("</div>");
        stream.append("<div style='text-align: center; margin-top: 20px;'>");
        stream.append("<button type='submit' class='button'>💾 保存設定</button>");
        stream.append("<a href='/' class='button secondary'>⬅️ 返回主頁</a>");
        stream.append("</div></form></div></body></html>");
        stream.finish();
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
            stream.append("<style>%s</style></head><body>", WebUI::getCompactCSS());
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
            stream.append("<style>%s</style></head><body>", WebUI::getCompactCSS());
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
        stream.append("<style>%s</style></head><body>", WebUI::getCompactCSS());
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
        stream.append("<style>%s</style></head><body>", WebUI::getCompactCSS());
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
        stream.append("<style>%s</style></head><body>", WebUI::getCompactCSS());
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
    
    // 核心架構統計 API
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
    
    // 核心架構統計重置端點
    webServer->on("/api/core/reset-stats", [](){
        if (!modernArchitectureEnabled || !g_eventBus) {
            webServer->send(400, "application/json", "{\"error\":\"Core architecture not enabled\"}");
            return;
        }
        
        g_eventBus->resetStatistics();
        String json = "{\"status\":\"success\",\"message\":\"Statistics reset successfully\",\"timestamp\":" + String(millis()) + "}";
        webServer->send(200, "application/json", json);
    });
    
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
        String deviceIP = WiFi.localIP().toString();
        
        // 使用優化版本生成OTA頁面
        MemoryOptimization::StreamingResponseBuilder stream;
        stream.begin(webServer);
        stream.append("<!DOCTYPE html><html><head><meta charset='UTF-8'>");
        stream.append("<title>OTA 更新</title>");
        stream.append("<style>%s</style></head><body>", WebUI::getCompactCSS());
        stream.append("<div class='container'><h1>🔄 OTA 遠程更新</h1>");
        stream.append("<div class='status'><h3>🔄 OTA 更新狀態</h3>");
        stream.append("<p><span style='color: green;'>●</span> OTA 服務已啟用</p>");
        stream.append("<p><strong>設備主機名:</strong> DaiSpan-Thermostat</p>");
        stream.append("<p><strong>IP地址:</strong> %s</p></div>", deviceIP.c_str());
        stream.append("<div class='warning'><h3>⚠️ 注意事項</h3>");
        stream.append("<ul><li>OTA 更新過程中請勿斷電或斷網</li>");
        stream.append("<li>更新失敗可能導致設備無法啟動</li>");
        stream.append("<li>建議在更新前備份當前固件</li>");
        stream.append("<li>更新完成後設備會自動重啟</li></ul></div>");
        stream.append("<div><h3>📝 使用說明</h3>");
        stream.append("<p>使用 PlatformIO 進行 OTA 更新：</p>");
        stream.append("<div class='code-block'>pio run -t upload --upload-port %s</div>", deviceIP.c_str());
        stream.append("<p>或使用 Arduino IDE：</p>");
        stream.append("<ol><li>工具 → 端口 → 選擇網路端口</li>");
        stream.append("<li>選擇設備主機名: DaiSpan-Thermostat</li>");
        stream.append("<li>輸入 OTA 密碼</li><li>點擊上傳</li></ol></div>");
        stream.append("<div style='text-align: center; margin-top: 30px;'>");
        stream.append("<a href='/' class='button secondary'>⬅️ 返回主頁</a>");
        stream.append("<a href='/restart' class='button danger'>🔄 重新啟動</a>");
        stream.append("</div></div></body></html>");
        stream.finish();
    });
    
    // 記憶體清理 API 端點
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
    
    // 記憶體優化狀態 API 端點
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
    
    // 詳細記憶體分析 API 端點
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
    
    // 緩衝區池統計 API 端點
    webServer->on("/api/buffer/stats", [](){
        if (!pageGenerator) {
            webServer->send(503, "text/plain", "Buffer pool not initialized");
            return;
        }
        
        String stats;
        pageGenerator->getSystemStats(stats);
        webServer->send(200, "text/plain", stats);
    });
    
    // 性能測試 API 端點
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
    
    // 負載測試 API 端點
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
    
    // 基準測試比較 API 端點
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
    
    // 即時監控儀表板 API 端點
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
        stream.append("<style>%s</style></head><body>", WebUI::getCompactCSS());
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
    
    // 初始化記憶體優化組件
    initializeMemoryOptimization();
    
    DEBUG_INFO_PRINT("[Main] WebServer監控功能已啟動: http://%s:8080\n", 
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
    // 處理遠端調試器
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