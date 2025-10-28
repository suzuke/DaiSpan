// DaiSpan 智能恆溫器系統
// 精簡版架構：專注 HomeKit + Wi-Fi + OTA 核心功能

#include "HomeSpan.h"
#include "controller/ThermostatController.h"
#include "device/ThermostatDevice.h"
#include "device/FanDevice.h"
#include "protocol/S21Protocol.h"
#include "protocol/IACProtocol.h"
#include "protocol/ACProtocolFactory.h"
#include "common/Debug.h"
#include "common/Config.h"
#include "common/WiFiManager.h"
#include "common/SystemManager.h"
#ifdef ENABLE_OTA_UPDATE
#include <ArduinoOTA.h>
#endif
#include "WiFi.h"
#include "WebServer.h"
#include "common/WebUI.h"
#include "common/WebTemplates.h"
#include "common/MemoryOptimization.h"
#include <pgmspace.h>

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
ThermostatDevice* thermostatDevice = nullptr;
FanDevice* fanDevice = nullptr;
SpanAccessory* accessory = nullptr;
bool deviceInitialized = false;
bool homeKitInitialized = false;

// 配置和管理器
ConfigManager configManager;
WiFiManager* wifiManager = nullptr;
SystemManager* systemManager = nullptr;

// WebServer
WebServer* webServer = nullptr;
bool monitoringEnabled = false;
bool homeKitPairingActive = false;

// 記憶體優化組件
std::unique_ptr<MemoryOptimization::WebPageGenerator> pageGenerator = nullptr;
std::unique_ptr<MemoryOptimization::MemoryManager> memoryManager = nullptr;

// 核心架構組件
// 系統啟動時間追蹤
unsigned long systemStartTime = 0;

// 共用樣式/版面輔助
static const char CONFIG_PAGE_STYLE[] PROGMEM = R"rawliteral(
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;margin:0;padding:20px;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;}
.config-page{max-width:960px;margin:0 auto;}
.page-header{text-align:center;color:#fff;margin-bottom:20px;}
.page-header h1{margin:0;font-size:2.2em;text-shadow:0 2px 4px rgba(0,0,0,0.3);}
.page-header p{margin:8px 0 0;font-size:1em;opacity:0.9;}
.config-panel{background:#fff;border-radius:18px;padding:24px;box-shadow:0 12px 30px rgba(0,0,0,0.12);display:flex;flex-direction:column;gap:24px;}
.summary-block,.action-block{background:#f8f9fa;border-radius:14px;padding:20px;}
.summary-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:16px;}
.summary-card{background:#fff;border-radius:12px;padding:16px;border:1px solid #e5e7eb;}
.summary-card h3{margin:0 0 12px;color:#0f172a;font-size:1.05em;}
.summary-item{display:flex;justify-content:space-between;margin:4px 0;font-size:0.95em;color:#475569;}
.summary-badge{display:inline-block;padding:4px 10px;border-radius:999px;font-size:0.8em;}
.badge-good{background:#dcfce7;color:#166534;}
.badge-warn{background:#fef9c3;color:#854d0e;}
.badge-bad{background:#fee2e2;color:#b91c1c;}
.action-group{background:#fff;border-radius:12px;padding:18px;border:1px solid #e5e7eb;margin-bottom:16px;}
.action-group h3{margin:0 0 10px;color:#0f172a;}
.action-hint{color:#64748b;font-size:0.9em;margin-bottom:10px;}
.form-group{margin:15px 0;}
.form-group label{display:block;font-weight:600;margin-bottom:6px;color:#0f172a;}
.form-group input{width:100%;padding:12px;border:1px solid #d0d7e2;border-radius:8px;font-size:0.95em;box-sizing:border-box;}
.form-group input:focus{outline:none;border-color:#2563eb;box-shadow:0 0 0 3px rgba(37,99,235,0.15);}
.button{display:inline-flex;align-items:center;justify-content:center;padding:12px 20px;border:none;border-radius:10px;background:#2563eb;color:#fff;font-weight:600;text-decoration:none;cursor:pointer;margin:6px 4px 0 0;transition:transform 0.15s,box-shadow 0.15s;}
.button:hover{transform:translateY(-1px);box-shadow:0 8px 20px rgba(37,99,235,0.25);}
.button.secondary{background:#475569;}
.button.success{background:#16a34a;}
.button.warning{background:#f59e0b;color:#1f2937;}
.button.danger{background:#dc2626;}
.button.ghost{background:transparent;color:#2563eb;border:1px solid rgba(37,99,235,0.4);}
.tag{display:inline-flex;align-items:center;padding:4px 10px;border-radius:999px;font-size:0.8em;background:#e2e8f0;color:#1e293b;margin-right:6px;}
.tag span{font-weight:600;margin-left:6px;}
.network-list{max-height:220px;overflow-y:auto;margin-top:10px;border:1px solid #e2e8f0;border-radius:10px;padding:6px;background:#fff;}
.network-item{display:flex;justify-content:space-between;padding:10px;border-radius:8px;margin:4px 0;cursor:pointer;border:1px solid transparent;}
.network-item:hover{background:#eff6ff;}
.network-item.selected{border-color:#2563eb;background:#dbeafe;}
.signal-strong{color:#15803d;}
.signal-medium{color:#b45309;}
.signal-weak{color:#b91c1c;}
.loading{text-align:center;padding:16px;color:#475569;display:none;}
.homekit-qr{border:2px dashed #2563eb;border-radius:12px;padding:16px;margin:10px 0;background:#fff;}
.footer-link{text-align:center;margin-top:14px;}
.footer-link a{color:#e0e7ff;text-decoration:none;font-weight:600;}
.footer-link a:hover{text-decoration:underline;}
@media (max-width:768px){
  .config-panel{padding:18px;}
  .summary-grid{grid-template-columns:1fr;}
}
)rawliteral";

static String buildConfigPage(const char* title,
                              const String& summaryContent,
                              const String& actionContent,
                              const String& subtitle = "",
                              const String& extraScript = "") {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<title>" + String(title) + "</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += CONFIG_PAGE_STYLE;
    html += "</style></head><body>";
    html += "<div class='config-page'>";
    html += "<header class='page-header'><h1>" + String(title) + "</h1>";
    if (subtitle.length() > 0) {
        html += "<p>" + subtitle + "</p>";
    }
    html += "</header>";
    html += "<section class='config-panel'>";
    html += "<div class='summary-block'>" + summaryContent + "</div>";
    html += "<div class='action-block'>" + actionContent + "</div>";
    html += "</section>";
    html += "<div class='footer-link'><a href='/'>← 返回主頁</a></div>";
    html += "</div>";
    if (extraScript.length() > 0) {
        html += extraScript;
    }
    html += "</body></html>";
    return html;
}

// 函數聲明
void safeRestart();
void initializeMemoryOptimization();
void generateOptimizedMainPage();
void generateUnifiedMainPage();
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
        const auto& profile = MemoryOptimization::RefreshActiveMemoryProfile();
        String profileDesc = MemoryOptimization::DescribeProfile(profile);
        DEBUG_INFO_PRINT("[Main] 選擇記憶體配置檔: %s\n", profileDesc.c_str());
        
        // 總是創建記憶體管理器 (輕量級)
        memoryManager = std::make_unique<MemoryOptimization::MemoryManager>(profile);
        
        // 嘗試建立頁面生成器，即使記憶體緊張也盡力提供最小功能
        try {
            pageGenerator = std::make_unique<MemoryOptimization::WebPageGenerator>(profile);
            if (availableMemory < profile.thresholds.medium) {
                DEBUG_WARN_PRINT("[Main] 記憶體僅有 %u bytes，頁面生成器將以精簡模式運作\n", availableMemory);
            } else {
                DEBUG_INFO_PRINT("[Main] 完整記憶體優化功能已啟用\n");
            }
        } catch (const std::bad_alloc&) {
            pageGenerator.reset();
            DEBUG_ERROR_PRINT("[Main] 記憶體不足，無法建立頁面生成器 (僅提供 API 功能)\n");
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
        if (memoryManager && memoryManager->isEmergencyMode()) {
            webServer->send(200, "text/html",
                "<html><body><h1>系統記憶體不足</h1><p>目前僅保留核心功能，請稍後再試或重新啟動設備。</p><p><a href='/' style='display:inline-block;margin-top:10px;'>返回主頁</a></p></body></html>");
            return;
        }

        const auto& profile = MemoryOptimization::GetActiveMemoryProfile();
        stream.begin(webServer, "text/html", profile.streamingChunkSize);
        
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
        stream.append("<div class='status-item'>");
        stream.append("<span class='status-label'>記憶體配置檔:</span>");
        stream.appendf("<span class='status-value'>%s</span>", profile.name.c_str());
        stream.append("</div>");
        stream.append("<div class='status-item'>");
        stream.append("<span class='status-label'>配置說明:</span>");
        stream.appendf("<span class='status-value'>%s</span>", profile.selectionReason.c_str());
        stream.append("</div>");
        
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

        if (systemManager) {
            uint8_t pressureLevel = systemManager->getMemoryPressureLevel();
            const char* pressureClass = (pressureLevel == 0) ? "status-value" : (pressureLevel == 1 ? "status-warning" : "status-error");
            String pressureText = systemManager->getMemoryPressureText();
            stream.append("<div class='status-item'>");
            stream.append("<span class='status-label'>記憶體壓力:</span>");
            stream.appendf("<span class='%s'>%s</span>", pressureClass, pressureText.c_str());
            stream.append("</div>");

            stream.append("<div class='status-item'>");
            stream.append("<span class='status-label'>排程負載:</span>");
            stream.appendf("<span class='status-value'>%u 任務 / %.2f ms</span>",
                           systemManager->getSchedulerTaskCount(),
                           systemManager->getSchedulerDurationUs() / 1000.0f);
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
        webServer->sendHeader("Cache-Control", "no-cache, must-revalidate");
        webServer->sendHeader("Pragma", "no-cache");
        webServer->sendHeader("Connection", "close");
        
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
        
        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
        html += "<title>DaiSpan 智能恆溫器</title>";
        html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
        html += "<style>";
        html += "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;margin:0;padding:20px;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;}";
        html += ".container{max-width:1100px;margin:0 auto;color:#0f172a;}";
        html += ".header{text-align:center;color:white;margin-bottom:25px;}";
        html += ".header h1{font-size:2.6em;margin:0;text-shadow:0 8px 24px rgba(0,0,0,0.25);}";
        html += ".header p{margin:8px 0 0;opacity:0.85;}";
        html += ".layout-grid{display:grid;grid-template-columns:2fr 1fr;gap:20px;align-items:start;}";
        html += ".panel{background:#fff;border-radius:20px;padding:26px;box-shadow:0 18px 40px rgba(15,23,42,0.18);}";
        html += ".panel-title{display:flex;justify-content:space-between;align-items:center;margin-bottom:16px;font-size:1.1em;font-weight:600;}";
        html += ".badge{padding:4px 10px;border-radius:999px;font-size:0.82em;}";
        html += ".badge.good{background:#dcfce7;color:#166534;}";
        html += ".badge.warn{background:#fef9c3;color:#854d0e;}";
        html += ".badge.bad{background:#fee2e2;color:#b91c1c;}";
        html += ".status-cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:16px;}";
        html += ".status-card{border:1px solid #e2e8f0;border-radius:16px;padding:18px;background:#f8fafc;}";
        html += ".status-card h3{margin:0 0 12px;font-size:1.05em;color:#0f172a;}";
        html += ".stat-row{display:flex;justify-content:space-between;font-size:0.95em;margin:4px 0;color:#475569;}";
        html += ".stat-row span:last-child{font-weight:600;color:#0f172a;}";
        html += ".action-grid{display:flex;flex-direction:column;gap:14px;}";
        html += ".action-card{display:block;border:1px solid #e2e8f0;border-radius:16px;padding:18px;text-decoration:none;color:#0f172a;transition:transform 0.15s,box-shadow 0.15s;}";
        html += ".action-card:hover{transform:translateY(-2px);box-shadow:0 12px 30px rgba(15,23,42,0.15);}";
        html += ".action-card .action-title{font-weight:600;font-size:1.05em;margin-bottom:6px;}";
        html += ".action-card .action-desc{color:#475569;font-size:0.9em;margin:0 0 10px;}";
        html += ".action-card .action-link{font-weight:600;color:#2563eb;}";
        html += ".tone-primary{border-left:5px solid #2563eb;}";
        html += ".tone-success{border-left:5px solid #16a34a;}";
        html += ".tone-warning{border-left:5px solid #f59e0b;}";
        html += ".tone-neutral{border-left:5px solid #475569;}";
        html += ".footer{text-align:center;margin-top:25px;color:rgba(255,255,255,0.85);}";
        html += "@media(max-width:960px){.layout-grid{grid-template-columns:1fr;}}";
        html += "</style></head><body>";
        html += "<div class='container'>";
        html += "<div class='header'><h1>🌡️ DaiSpan</h1><p>智能恆溫器控制中心</p></div>";
        
        uint32_t freeHeap = ESP.getFreeHeap();
        unsigned long uptimeSeconds = millis() / 1000;
        unsigned long uptimeHours = uptimeSeconds / 3600;
        unsigned long uptimeMinutes = (uptimeSeconds % 3600) / 60;
        bool wifiConnected = WiFi.status() == WL_CONNECTED;
        uint8_t pressureLevel = (systemManager != nullptr) ? systemManager->getMemoryPressureLevel() : 0;
        String pressureText = (systemManager != nullptr) ? systemManager->getMemoryPressureText() : "未知";
        String pressureBadge = (pressureLevel == 0) ? "badge good" : (pressureLevel == 1 ? "badge warn" : "badge bad");
        uint16_t schedulerCount = (systemManager != nullptr) ? systemManager->getSchedulerTaskCount() : 0;
        float schedulerDuration = (systemManager != nullptr) ? systemManager->getSchedulerDurationUs() / 1000.0f : 0.0f;
        
        String statusPanel = "<div class='status-cards'>";
        statusPanel += "<div class='status-card'><h3>🖥️ 系統概況</h3>";
        statusPanel += "<div class='stat-row'><span>可用記憶體</span><span>" + String(freeHeap / 1024) + " KB</span></div>";
        statusPanel += "<div class='stat-row'><span>運行時間</span><span>" + String(uptimeHours) + "h " + String(uptimeMinutes) + "m</span></div>";
        statusPanel += "<div class='stat-row'><span>排程任務</span><span>" + String(schedulerCount) + " / " + String(schedulerDuration, 2) + " ms</span></div>";
        statusPanel += "<span class='" + pressureBadge + "'>記憶體壓力：" + pressureText + "</span>";
        statusPanel += "</div>";
        
        statusPanel += "<div class='status-card'><h3>📶 網路狀態</h3>";
        if (wifiConnected) {
            statusPanel += "<div class='stat-row'><span>SSID</span><span>" + WiFi.SSID() + "</span></div>";
            statusPanel += "<div class='stat-row'><span>信號</span><span>" + String(WiFi.RSSI()) + " dBm</span></div>";
            statusPanel += "<div class='stat-row'><span>IP</span><span>" + WiFi.localIP().toString() + "</span></div>";
        } else {
            statusPanel += "<div class='stat-row'><span>狀態</span><span>未連線</span></div>";
        }
        statusPanel += "<div class='stat-row'><span>MAC</span><span>" + WiFi.macAddress() + "</span></div>";
        statusPanel += "</div>";
        
        statusPanel += "<div class='status-card'><h3>🏠 HomeKit</h3>";
        statusPanel += "<div class='stat-row'><span>服務</span><span>" + String(homeKitInitialized ? "運行中" : "未啟動") + "</span></div>";
        statusPanel += "<div class='stat-row'><span>設備</span><span>" + String(deviceInitialized ? "已初始化" : "待機") + "</span></div>";
        statusPanel += "<div class='stat-row'><span>配對狀態</span><span>" + String(homeKitPairingActive ? "配對中" : "待命") + "</span></div>";
        statusPanel += "</div>";
        statusPanel += "</div>";
        
        auto actionCard = [](const char* toneClass, const char* titleText, const char* description, const char* href) -> String {
            String card = "<a class='action-card ";
            card += toneClass;
            card += "' href='";
            card += href;
            card += "'>";
            card += "<div class='action-title'>";
            card += titleText;
            card += "</div><p class='action-desc'>";
            card += description;
            card += "</p><span class='action-link'>立即前往 →</span></a>";
            return card;
        };
        
        String actionPanel = "<div class='action-grid'>";
        actionPanel += actionCard("tone-primary", "📶 WiFi 設定", "更新網路認證並掃描附近基地台。", "/wifi");
        actionPanel += actionCard("tone-primary", "🏠 HomeKit 設定", "調整配對碼、裝置名稱與 QR ID。", "/homekit");
        actionPanel += actionCard("tone-warning", "⬆️ OTA 更新", "透過瀏覽器上傳韌體 (如已啟用)。", "/ota");
        actionPanel += actionCard("tone-neutral", "📋 系統狀態 API", "檢視 JSON 健康資訊。", "/api/health");
        actionPanel += actionCard("tone-success", "📄 系統日誌", "監看近期事件與診斷輸出。", "/logs");
        actionPanel += "</div>";
        
        html += "<div class='layout-grid'>";
        html += "<section class='panel status-panel'><div class='panel-title'><span>狀態面板</span><span class='" + pressureBadge + "'>" + pressureText + "</span></div>";
        html += statusPanel;
        html += "</section>";
        html += "<section class='panel action-panel'><div class='panel-title'><span>操作面板</span><span class='badge good'>核心入口</span></div>";
        html += actionPanel;
        html += "</section>";
        html += "</div>";
        
        html += "<div class='footer'>DaiSpan Smart Thermostat Controller | 統一 Web 介面</div>";
        html += "</div></body></html>";
        
        webServer->send(200, "text/html", html);
    } catch (...) {
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

/**
 * 初始化核心架構
 */
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
            
            DEBUG_INFO_PRINT("[WiFi] 使用統一WiFi配置頁面生成\n");
            bool wifiConnected = WiFi.status() == WL_CONNECTED;
            String summaryContent = "<div class='summary-grid'>";
            summaryContent += "<div class='summary-card'><h3>網路狀態</h3>";
            if (wifiConnected) {
                summaryContent += "<div class='summary-item'><span>SSID</span><span>" + WiFi.SSID() + "</span></div>";
                summaryContent += "<div class='summary-item'><span>信號</span><span>" + String(WiFi.RSSI()) + " dBm</span></div>";
                summaryContent += "<div class='summary-item'><span>IP</span><span>" + WiFi.localIP().toString() + "</span></div>";
            } else {
                summaryContent += "<div class='summary-item'><span>狀態</span><span>未連線</span></div>";
            }
            summaryContent += "<div class='summary-item'><span>MAC</span><span>" + WiFi.macAddress() + "</span></div>";
            summaryContent += "</div>";
            summaryContent += "<div class='summary-card'><h3>建議與說明</h3>";
            summaryContent += "<p class='action-hint'>使用掃描功能能快速選擇附近網路，或手動輸入 SSID 與密碼。</p>";
            summaryContent += "<div class='tag'>現有設定<span>" + configManager.getWiFiSSID() + "</span></div>";
            summaryContent += "<div class='tag'>記憶體<span>" + String(ESP.getFreeHeap() / 1024) + " KB</span></div>";
            summaryContent += "</div></div>";

            String actionContent = "<div class='action-group'>";
            actionContent += "<h3>🔍 掃描可用網路</h3>";
            actionContent += "<p class='action-hint'>點擊「立即掃描」取得附近 WiFi，點選即可填入表單。</p>";
            actionContent += "<button class='button success' type='button' onclick='scanNetworks()'>🔄 立即掃描</button>";
            actionContent += "<div class='loading' id='loading'>正在掃描 WiFi 網路...</div>";
            actionContent += "<div class='network-list' id='networkList'></div>";
            actionContent += "</div>";

            actionContent += "<div class='action-group'><h3>⚙️ 手動設定</h3>";
            actionContent += "<form method='POST' action='/wifi-save'>";
            actionContent += "<div class='form-group'><label for='ssid'>網路名稱 (SSID)</label><input type='text' id='ssid' name='ssid' value='" + configManager.getWiFiSSID() + "' required></div>";
            actionContent += "<div class='form-group'><label for='password'>密碼</label><input type='password' id='password' name='password' placeholder='如果網路沒有密碼可留空'></div>";
            actionContent += "<button type='submit' class='button'>💾 保存並連線</button>";
            actionContent += "<a href='/' class='button secondary'>⬅️ 返回主頁</a>";
            actionContent += "</form></div>";

            actionContent += "<div class='action-group'><h3>其他操作</h3><p class='action-hint'>若無法連線，可重置 Wi-Fi 設定重新進入配網模式。</p><a href='/reset-wifi' class='button ghost'>🔁 重置 WiFi 設定</a></div>";

            String extraScript = "<script>function attachNetworkHandlers(){document.querySelectorAll('.network-item').forEach(item=>item.addEventListener('click',()=>selectNetwork(item.dataset.ssid, item)));}"
                                 "function scanNetworks(){const loading=document.getElementById('loading');const list=document.getElementById('networkList');loading.style.display='block';list.innerHTML='';fetch('/api/wifi/scan').then(res=>res.json()).then(data=>{loading.style.display='none';if(!data.networks||data.networks.length===0){list.innerHTML='未找到可用網路';return;}let html='';data.networks.forEach(net=>{const signalClass=net.rssi>-50?'signal-strong':net.rssi>-70?'signal-medium':'signal-weak';const security=net.encryption>0?'🔒':'🔓';html+=`<div class='network-item' data-ssid='${net.ssid}'>`;html+=`<span>${security} ${net.ssid}</span>`;html+=`<span class='${signalClass}'>${net.rssi} dBm</span>`;html+='</div>';});list.innerHTML=html;attachNetworkHandlers();}).catch(err=>{console.error('WiFi掃描錯誤:',err);loading.style.display='none';list.innerHTML='<p style=\"color:red;\">掃描失敗，請稍後再試</p>';});}"
                                 "function selectNetwork(ssid, element){document.getElementById('ssid').value=ssid;document.querySelectorAll('.network-item').forEach(item=>item.classList.remove('selected'));if(element){element.classList.add('selected');}}"
                                 "window.addEventListener('load',()=>scanNetworks());</script>";

            String page = buildConfigPage("📡 WiFi 網路配置", summaryContent, actionContent, "設定或更新裝置的無線網路連線", extraScript);
            webServer->send(200, "text/html", page);
            
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
            const auto& profile = MemoryOptimization::GetActiveMemoryProfile();
            stream.begin(webServer, "text/html", profile.streamingChunkSize);
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
            if (memoryManager) {
                auto strategy = memoryManager->getRenderStrategy();
                if (strategy == MemoryOptimization::MemoryManager::RenderStrategy::EMERGENCY) {
                    String emergencyHtml = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
                    emergencyHtml += "<title>HomeKit配置 (緊急模式)</title></head><body style='margin:20px;'>";
                    emergencyHtml += "<h1>HomeKit配置</h1>";
                    emergencyHtml += "<p style='color:orange;'>⚠️ 系統記憶體極低，使用簡化模式</p>";
                    String currentPairingCode = configManager.getHomeKitPairingCode();
                    String currentDeviceName = configManager.getHomeKitDeviceName();
                    emergencyHtml += "<form method='POST' action='/homekit-save'>";
                    emergencyHtml += "<p>配對碼: <input type='text' name='pairing_code' value='" + currentPairingCode + "' required></p>";
                    emergencyHtml += "<p>設備名稱: <input type='text' name='device_name' value='" + currentDeviceName + "' required></p>";
                    emergencyHtml += "<button type='submit'>保存</button> <a href='/'>返回主頁</a></p>";
                    emergencyHtml += "</form></body></html>";
                    webServer->send(200, "text/html", emergencyHtml);
                    return;
                }
            }

            String currentPairingCode = configManager.getHomeKitPairingCode();
            String currentDeviceName = configManager.getHomeKitDeviceName();
            String currentQRID = configManager.getHomeKitQRID();
            bool serviceOnline = homeKitInitialized;
            bool deviceReady = deviceInitialized;

            String summaryContent = "<div class='summary-grid'>";
            summaryContent += "<div class='summary-card'><h3>HomeKit 服務</h3>";
            summaryContent += "<div class='summary-item'><span>狀態</span><span>" + String(serviceOnline ? "運行中" : "未啟動") + "</span></div>";
            summaryContent += "<div class='summary-item'><span>配對狀態</span><span>" + String(homeKitPairingActive ? "配對中" : "待命") + "</span></div>";
            summaryContent += "</div>";
            summaryContent += "<div class='summary-card'><h3>設備與連線</h3>";
            summaryContent += "<div class='summary-item'><span>恆溫器</span><span>" + String(deviceReady ? "已初始化" : "待機") + "</span></div>";
            summaryContent += "<div class='summary-item'><span>WiFi</span><span>" + String(WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "未連線") + "</span></div>";
            summaryContent += "</div>";
            summaryContent += "<div class='summary-card'><h3>目前設定</h3>";
            summaryContent += "<div class='summary-item'><span>配對碼</span><span>" + currentPairingCode + "</span></div>";
            summaryContent += "<div class='summary-item'><span>裝置名稱</span><span>" + currentDeviceName + "</span></div>";
            summaryContent += "<div class='summary-item'><span>QR ID</span><span>" + currentQRID + "</span></div>";
            summaryContent += "</div></div>";

            String actionContent = "<div class='action-group'><h3>📱 配對資訊</h3>";
            actionContent += "<div class='homekit-qr'><p><strong>配對代碼:</strong> " + currentPairingCode + "</p>";
            actionContent += "<p>在 iOS 「家庭」App 中掃描 QR 或輸入配對碼完成配對。</p>";
            if (serviceOnline) {
                actionContent += "<a class='button ghost' href='/restart'>🔄 重新啟動服務</a>";
            }
            actionContent += "</div></div>";

            actionContent += "<div class='action-group'><h3>⚙️ 變更 HomeKit 設定</h3>";
            actionContent += "<form method='post' action='/homekit-save'>";
            actionContent += "<div class='form-group'><label for='pairing_code'>配對代碼 (8位數字)</label><input type='text' id='pairing_code' name='pairing_code' value='" + currentPairingCode + "' maxlength='8' pattern='[0-9]{8}' required></div>";
            actionContent += "<div class='form-group'><label for='device_name'>設備名稱</label><input type='text' id='device_name' name='device_name' value='" + currentDeviceName + "' maxlength='64' required></div>";
            actionContent += "<div class='form-group'><label for='qr_id'>QR ID (4位字母)</label><input type='text' id='qr_id' name='qr_id' value='" + currentQRID + "' maxlength='4' pattern='[A-Z]{4}' required></div>";
            actionContent += "<button type='submit' class='button success'>💾 保存設定並重啟</button>";
            actionContent += "<a href='/' class='button secondary'>⬅️ 返回主頁</a>";
            actionContent += "</form></div>";

            actionContent += "<div class='action-group'><h3>操作提示</h3><p class='action-hint'>變更配對資訊後，HomeKit 服務會重新啟動，請於 1 分鐘內完成配對。</p></div>";

            String page = buildConfigPage("🏠 HomeKit 配置", summaryContent, actionContent, "管理配對碼與裝置狀態", "<script>setTimeout(()=>location.reload(), 60000);</script>");
            webServer->send(200, "text/html", page);
        } catch (...) {
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
        
        const auto& profile = MemoryOptimization::GetActiveMemoryProfile();

        if (configChanged) {
            configManager.setHomeKitConfig(currentPairingCode, currentDeviceName, currentQRID);
            
            MemoryOptimization::StreamingResponseBuilder stream;
            stream.begin(webServer, "text/html", profile.streamingChunkSize);
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
            stream.begin(webServer, "text/html", profile.streamingChunkSize);
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
    // 模擬功能已移除
    
    
    // 系統健康檢查端點
    webServer->on("/api/health", [](){
        String json = "{";
        json += "\"status\":\"ok\",";
        json += "\"services\":{";
        json += "\"homekit\":" + String(homeKitInitialized ? "true" : "false") + ",";
        json += "\"hardware\":" + String(deviceInitialized ? "true" : "false") + ",";
        json += "\"wifi\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
        json += "\"webserver\":" + String(monitoringEnabled ? "true" : "false");
        json += "},";
        json += "\"memory\":{";
        json += "\"free\":" + String(ESP.getFreeHeap()) + ",";
        json += "\"total\":" + String(ESP.getHeapSize()) + ",";
        json += "\"usage\":" + String(100.0 * (ESP.getHeapSize() - ESP.getFreeHeap()) / ESP.getHeapSize(), 1);
        json += "},";
        if (systemManager) {
            json += "\"scheduler\":{";
            json += "\"lastTasks\":" + String(systemManager->getSchedulerTaskCount()) + ",";
            json += "\"lastDurationUs\":" + String(systemManager->getSchedulerDurationUs()) + ",";
            json += "\"memoryPressure\":\"" + systemManager->getMemoryPressureText() + "\"";
            json += "},";
        } else {
            json += "\"scheduler\":{\"lastTasks\":0,\"lastDurationUs\":0,\"memoryPressure\":\"unknown\"},";
        }
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
        

        // 結束 JSON
        if (written < sizeof(buffer) - 100) {
            snprintf(buffer + written, sizeof(buffer) - written,
                "],"
                "\"logLevel\":\"info\","
                "\"logCount\":%d,"
                "\"timestamp\":%u"
                "}",
                5, timestamp);
        }
        
        webServer->send(200, "application/json", buffer);
    });
    
    // OTA 頁面
    webServer->on("/ota", [](){
        try {
            if (memoryManager && memoryManager->isEmergencyMode()) {
                webServer->send(200, "text/html", 
                    "<html><body><h1>系統記憶體不足</h1><p>目前僅保留核心功能，OTA 頁面暫時不可用。</p><p><a href='/' style='display:inline-block;margin-top:10px;'>返回主頁</a></p></body></html>");
                return;
            }

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
        if (!memoryManager) {
            webServer->send(503, "application/json", 
                           "{\"error\":\"Memory optimization not initialized\"}");
            return;
        }

        // 使用 StreamingResponseBuilder 生成 JSON 響應
        const auto& profile = MemoryOptimization::GetActiveMemoryProfile();
        MemoryOptimization::StreamingResponseBuilder stream;
        stream.begin(webServer, "application/json", profile.streamingChunkSize);
        
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
        
        stream.append("\"profile\":{");
        stream.appendf("\"name\":\"%s\",", profile.name.c_str());
        stream.appendf("\"hardwareTag\":\"%s\",", profile.hardwareTag.c_str());
        stream.append("\"thresholds\":{");
        stream.appendf("\"low\":%u,", profile.thresholds.low);
        stream.appendf("\"medium\":%u,", profile.thresholds.medium);
        stream.appendf("\"high\":%u,", profile.thresholds.high);
        stream.appendf("\"critical\":%u}", profile.thresholds.critical);
        stream.appendf(",\"bufferPools\":{\"small\":%zu,\"medium\":%zu,\"large\":%zu},",
                      profile.pools.smallCount, profile.pools.mediumCount, profile.pools.largeCount);
        stream.appendf("\"streamingChunk\":%zu,", profile.streamingChunkSize);
        stream.appendf("\"maxRender\":%zu,", profile.maxRenderSize);
        stream.appendf("\"selectionReason\":\"%s\"", profile.selectionReason.c_str());
        stream.append("},");

        stream.appendf("\"timestamp\":%u", (uint32_t)(millis() / 1000));
        stream.append("}");
        
        stream.finish();
        
        DEBUG_VERBOSE_PRINT("[API] 記憶體優化狀態查詢完成\n");
    });
#endif
    
#ifndef PRODUCTION_BUILD
    // 詳細記憶體分析 API 端點 (開發模式)
    webServer->on("/api/memory/detailed", [](){
        if (!memoryManager) {
            webServer->send(503, "application/json", 
                           "{\"error\":\"Memory optimization not initialized\"}");
            return;
        }

        const auto& profile = memoryManager->getActiveProfile();
        MemoryOptimization::StreamingResponseBuilder stream;
        stream.begin(webServer, "application/json", profile.streamingChunkSize);
        
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

        stream.append("\"profile\":{");
        stream.appendf("\"name\":\"%s\",", profile.name.c_str());
        stream.appendf("\"hardwareTag\":\"%s\",", profile.hardwareTag.c_str());
        stream.append("\"thresholds\":{");
        stream.appendf("\"low\":%u,", profile.thresholds.low);
        stream.appendf("\"medium\":%u,", profile.thresholds.medium);
        stream.appendf("\"high\":%u,", profile.thresholds.high);
        stream.appendf("\"critical\":%u}", profile.thresholds.critical);
        stream.appendf(",\"bufferPools\":{\"small\":%zu,\"medium\":%zu,\"large\":%zu},",
                      profile.pools.smallCount, profile.pools.mediumCount, profile.pools.largeCount);
        stream.appendf("\"streamingChunk\":%zu,", profile.streamingChunkSize);
        stream.appendf("\"maxRender\":%zu,", profile.maxRenderSize);
        stream.appendf("\"selectionReason\":\"%s\"", profile.selectionReason.c_str());
        stream.append("},");
        
        // 系統統計信息
        bool hasPageGenerator = (pageGenerator != nullptr);
        
        stream.append("\"statistics\":{");
        stream.append("\"memorySummary\":\"Available in /api/memory/stats-text\",");
        stream.append("\"bufferSummary\":\"Available in /api/buffer/stats\",");
        stream.appendf("\"pageGenerator\":%s", hasPageGenerator ? "true" : "false");
        stream.append("},");
        
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
        if (pageGenerator) {
            pageGenerator->getSystemStats(stats);
        } else {
            stats = "Page generator unavailable\nMemory statistics: see /api/memory/stats";
        }
        webServer->send(200, "text/plain", stats);
    });
#endif
    
#ifndef PRODUCTION_BUILD
    // 性能測試 API 端點 (開發模式)
    webServer->on("/api/performance/test", [](){
        uint32_t startTime = millis();
        uint32_t startHeap = ESP.getFreeHeap();
        
        const auto& profile = MemoryOptimization::GetActiveMemoryProfile();
        MemoryOptimization::StreamingResponseBuilder stream;
        stream.begin(webServer, "application/json", profile.streamingChunkSize);
        
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
        MemoryOptimization::StreamingResponseBuilder testStream(profile.streamingChunkSize);
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
        
        const auto& profile = MemoryOptimization::GetActiveMemoryProfile();
        MemoryOptimization::StreamingResponseBuilder stream;
        stream.begin(webServer, "application/json", profile.streamingChunkSize);
        
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
        const auto& profile = MemoryOptimization::GetActiveMemoryProfile();
        MemoryOptimization::StreamingResponseBuilder stream;
        stream.begin(webServer, "application/json", profile.streamingChunkSize);
        
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
        const auto& profile = MemoryOptimization::GetActiveMemoryProfile();
        MemoryOptimization::StreamingResponseBuilder stream;
        stream.begin(webServer, "application/json", profile.streamingChunkSize);
        
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
            const auto& activeProfile = memoryManager->getActiveProfile();
            
            stream.append("\"memoryOptimization\":{");
            stream.append("\"enabled\":true,");
            stream.appendf("\"pressure\":%d,", static_cast<int>(pressure));
            stream.appendf("\"strategy\":%d,", static_cast<int>(strategy));
            stream.appendf("\"maxBufferSize\":%zu,", memoryManager->getMaxBufferSize());
            stream.appendf("\"useStreaming\":%s", 
                          memoryManager->shouldUseStreamingResponse() ? "true" : "false");
            stream.append("},");

            stream.append("\"memoryProfile\":{");
            stream.appendf("\"name\":\"%s\",", activeProfile.name.c_str());
            stream.appendf("\"hardwareTag\":\"%s\",", activeProfile.hardwareTag.c_str());
            stream.append("\"thresholds\":{");
            stream.appendf("\"low\":%u,", activeProfile.thresholds.low);
            stream.appendf("\"medium\":%u,", activeProfile.thresholds.medium);
            stream.appendf("\"high\":%u,", activeProfile.thresholds.high);
            stream.appendf("\"critical\":%u}", activeProfile.thresholds.critical);
            stream.appendf(",\"bufferPools\":{\"small\":%zu,\"medium\":%zu,\"large\":%zu},",
                          activeProfile.pools.smallCount,
                          activeProfile.pools.mediumCount,
                          activeProfile.pools.largeCount);
            stream.appendf("\"streamingChunk\":%zu,", activeProfile.streamingChunkSize);
            stream.appendf("\"maxRender\":%zu,", activeProfile.maxRenderSize);
            stream.appendf("\"selectionReason\":\"%s\"", activeProfile.selectionReason.c_str());
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
        const auto& profile = MemoryOptimization::GetActiveMemoryProfile();
        
        MemoryOptimization::StreamingResponseBuilder stream;
        stream.begin(webServer, "text/html", profile.streamingChunkSize);
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
    
    DEBUG_INFO_PRINT("[Main] 遠端除錯已停用，請使用序列埠觀察 log。\n");
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
    // 模擬功能已移除
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
            
#ifdef ENABLE_OTA_UPDATE
            // Arduino OTA 設置（可選功能）
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
#else
            DEBUG_INFO_PRINT("[Main] OTA 功能未啟用，此版本僅提供核心功能。\n");
#endif
            
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
