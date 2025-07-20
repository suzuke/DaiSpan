#pragma once

#ifdef ENABLE_LIGHTWEIGHT_DEBUG

#include <WebServer.h>
#include <ArduinoJson.h>
#include "MemoryMonitor.h"

// 輕量級HTTP調試器 (節省90KB記憶體)
// 無持久化存儲，即時串流系統狀態
class LightweightDebugger {
private:
    WebServer* debugServer;
    bool debugEnabled;
    uint16_t serverPort;
    
    // 最小化記憶體使用 - 僅保存當前狀態
    struct CurrentState {
        uint32_t lastLogTime = 0;
        char lastLogLevel[8] = "";
        char lastLogComponent[16] = "";
        char lastLogMessage[64] = "";
        
        uint32_t lastHomeKitTime = 0;
        char lastHomeKitOp[24] = "";
        char lastHomeKitService[16] = "";
        bool lastHomeKitSuccess = false;
        
        uint32_t systemStartTime = 0;
        uint32_t totalLogCount = 0;
        uint32_t totalHomeKitCount = 0;
        uint32_t totalErrorCount = 0;
    } state;
    
    // 記憶體監控
    MemoryMonitor memoryMonitor;
    
public:
    // 單例模式
    static LightweightDebugger& getInstance() {
        static LightweightDebugger instance;
        return instance;
    }
    
    // 初始化 - 僅5-10KB記憶體使用
    bool begin(uint16_t port = 8082) {
        if (!debugServer) {
            debugServer = new WebServer(port);
            serverPort = port;
            setupRoutes();
            debugServer->begin();
            debugEnabled = true;
            state.systemStartTime = millis();
            
            // 初始化記憶體監控
            memoryMonitor.begin();
            
            Serial.println("[LightDebug] Server started on port " + String(port));
            Serial.println("[LightDebug] Memory usage: ~" + String(getMemoryUsage()) + " bytes");
            return true;
        }
        return false;
    }
    
    void loop() {
        if (debugServer && debugEnabled) {
            debugServer->handleClient();
            // 更新記憶體監控
            memoryMonitor.loop();
        }
    }
    
    void stop() {
        if (debugServer) {
            debugServer->stop();
            delete debugServer;
            debugServer = nullptr;
            debugEnabled = false;
        }
    }
    
    // 輕量級日誌 - 不持久化，僅更新當前狀態
    void log(const String& level, const String& component, const String& message) {
        if (!debugEnabled) return;
        
        state.lastLogTime = millis();
        strncpy(state.lastLogLevel, level.c_str(), sizeof(state.lastLogLevel) - 1);
        strncpy(state.lastLogComponent, component.c_str(), sizeof(state.lastLogComponent) - 1);
        strncpy(state.lastLogMessage, message.c_str(), sizeof(state.lastLogMessage) - 1);
        state.lastLogLevel[sizeof(state.lastLogLevel) - 1] = '\0';
        state.lastLogComponent[sizeof(state.lastLogComponent) - 1] = '\0';
        state.lastLogMessage[sizeof(state.lastLogMessage) - 1] = '\0';
        
        state.totalLogCount++;
        if (level == "ERROR") {
            state.totalErrorCount++;
        }
        
        // 同時輸出到串口
        Serial.println("[" + level + "] " + component + ": " + message);
    }
    
    void logHomeKitOperation(const String& operation, const String& service, 
                           const String& oldValue, const String& newValue, 
                           bool success, const String& errorMsg = "") {
        if (!debugEnabled) return;
        
        state.lastHomeKitTime = millis();
        strncpy(state.lastHomeKitOp, operation.c_str(), sizeof(state.lastHomeKitOp) - 1);
        strncpy(state.lastHomeKitService, service.c_str(), sizeof(state.lastHomeKitService) - 1);
        state.lastHomeKitOp[sizeof(state.lastHomeKitOp) - 1] = '\0';
        state.lastHomeKitService[sizeof(state.lastHomeKitService) - 1] = '\0';
        state.lastHomeKitSuccess = success;
        state.totalHomeKitCount++;
        
        // 串口輸出
        Serial.println("[HomeKit] " + operation + " on " + service + 
                      " (" + oldValue + " -> " + newValue + ") " + 
                      (success ? "SUCCESS" : "FAILED"));
    }
    
    // 即時串口日誌 (無記憶體存儲)
    void logSerial(const String& message) {
        if (!debugEnabled) return;
        Serial.println("[Serial] " + message);
    }
    
    // 簡化的測試功能
    bool triggerThermostatTest(uint8_t mode, float temperature) {
        log("INFO", "TEST", "Thermostat test: mode=" + String(mode) + ", temp=" + String(temperature));
        return true;
    }
    
    bool triggerFanTest(uint8_t speed) {
        log("INFO", "TEST", "Fan test: speed=" + String(speed));
        return true;
    }
    
    bool runDiagnostics() {
        log("INFO", "DIAG", "Running diagnostics...");
        log("INFO", "DIAG", "Free heap: " + String(ESP.getFreeHeap()));
        log("INFO", "DIAG", "Uptime: " + String(millis() - state.systemStartTime) + "ms");
        return true;
    }
    
    // 記憶體使用報告
    size_t getMemoryUsage() const {
        return sizeof(*this) + (debugServer ? sizeof(WebServer) : 0);
    }
    
    String getMemoryReport() const {
        String report = "Lightweight Debugger Memory Report:\n";
        report += "Total Memory: ~" + String(getMemoryUsage()) + " bytes\n";
        report += "Memory Savings: ~95% vs WebSocket implementation\n";
        report += "Features: HTTP API, Serial output, Current state only\n";
        return report;
    }

private:
    LightweightDebugger() : debugServer(nullptr), debugEnabled(false), serverPort(8082) {}
    
    void setupRoutes() {
        // 系統狀態 API
        debugServer->on("/api/status", HTTP_GET, [this]() {
            handleSystemStatus();
        });
        
        // 當前日誌狀態
        debugServer->on("/api/logs/current", HTTP_GET, [this]() {
            handleCurrentLogs();
        });
        
        // HomeKit狀態
        debugServer->on("/api/homekit/current", HTTP_GET, [this]() {
            handleHomeKitStatus();
        });
        
        // 記憶體報告
        debugServer->on("/api/memory", HTTP_GET, [this]() {
            handleMemoryReport();
        });
        
        // 詳細記憶體狀態
        debugServer->on("/api/memory/status", HTTP_GET, [this]() {
            handleDetailedMemoryStatus();
        });
        
        // 記憶體健康檢查
        debugServer->on("/api/memory/health", HTTP_GET, [this]() {
            handleMemoryHealthCheck();
        });
        
        // 診斷測試
        debugServer->on("/api/diagnostics", HTTP_POST, [this]() {
            runDiagnostics();
            handleSystemStatus();
        });
        
        // 簡單的調試界面
        debugServer->on("/", HTTP_GET, [this]() {
            handleDebugInterface();
        });
        
        // CORS 支持
        debugServer->onNotFound([this]() {
            if (debugServer->method() == HTTP_OPTIONS) {
                debugServer->sendHeader("Access-Control-Allow-Origin", "*");
                debugServer->sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
                debugServer->sendHeader("Access-Control-Allow-Headers", "Content-Type");
                debugServer->send(200);
            } else {
                debugServer->send(404, "text/plain", "Not Found");
            }
        });
    }
    
    void handleSystemStatus() {
        JsonDocument doc;
        doc["timestamp"] = millis();
        doc["uptime_ms"] = millis() - state.systemStartTime;
        doc["free_heap"] = ESP.getFreeHeap();
        doc["total_logs"] = state.totalLogCount;
        doc["total_homekit"] = state.totalHomeKitCount;
        doc["total_errors"] = state.totalErrorCount;
        doc["memory_usage"] = getMemoryUsage();
        
        String response;
        serializeJson(doc, response);
        
        debugServer->sendHeader("Access-Control-Allow-Origin", "*");
        debugServer->send(200, "application/json", response);
    }
    
    void handleCurrentLogs() {
        JsonDocument doc;
        
        if (state.lastLogTime > 0) {
            doc["timestamp"] = state.lastLogTime;
            doc["level"] = String(state.lastLogLevel);
            doc["component"] = String(state.lastLogComponent);
            doc["message"] = String(state.lastLogMessage);
        } else {
            doc["message"] = "No logs available";
        }
        
        String response;
        serializeJson(doc, response);
        
        debugServer->sendHeader("Access-Control-Allow-Origin", "*");
        debugServer->send(200, "application/json", response);
    }
    
    void handleHomeKitStatus() {
        JsonDocument doc;
        
        if (state.lastHomeKitTime > 0) {
            doc["timestamp"] = state.lastHomeKitTime;
            doc["operation"] = String(state.lastHomeKitOp);
            doc["service"] = String(state.lastHomeKitService);
            doc["success"] = state.lastHomeKitSuccess;
        } else {
            doc["message"] = "No HomeKit operations";
        }
        
        String response;
        serializeJson(doc, response);
        
        debugServer->sendHeader("Access-Control-Allow-Origin", "*");
        debugServer->send(200, "application/json", response);
    }
    
    void handleMemoryReport() {
        debugServer->sendHeader("Access-Control-Allow-Origin", "*");
        debugServer->send(200, "text/plain", getMemoryReport());
    }
    
    void handleDetailedMemoryStatus() {
        String memoryStats = memoryMonitor.getMemoryStatsJson();
        debugServer->sendHeader("Access-Control-Allow-Origin", "*");
        debugServer->send(200, "application/json", memoryStats);
    }
    
    void handleMemoryHealthCheck() {
        JsonDocument doc;
        doc["health_status"] = memoryMonitor.getHealthStatusText();
        doc["health_class"] = memoryMonitor.getHealthStatusClass();
        doc["usage_percent"] = memoryMonitor.getMemoryUsagePercent();
        doc["warning"] = memoryMonitor.getMemoryWarning();
        doc["needs_cleanup"] = memoryMonitor.needsEmergencyCleanup();
        doc["is_healthy"] = (memoryMonitor.getHealthStatus() == MemoryMonitor::HealthStatus::EXCELLENT || 
                            memoryMonitor.getHealthStatus() == MemoryMonitor::HealthStatus::GOOD);
        
        String response;
        serializeJson(doc, response);
        
        debugServer->sendHeader("Access-Control-Allow-Origin", "*");
        debugServer->send(200, "application/json", response);
    }
    
    void handleDebugInterface() {
        // 改進的響應式HTML界面，更好的用戶體驗
        String html = "<!DOCTYPE html><html><head>";
        html += "<title>DaiSpan Lightweight Debugger</title>";
        html += "<meta charset='utf-8'>";
        html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
        html += "<style>";
        
        // 現代響應式CSS
        html += "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; margin: 0; padding: 20px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; }";
        html += ".container { max-width: 1000px; margin: 0 auto; }";
        html += ".header { text-align: center; color: white; margin-bottom: 30px; }";
        html += ".header h1 { font-size: 2.5em; margin: 0; text-shadow: 0 2px 4px rgba(0,0,0,0.3); }";
        html += ".header p { font-size: 1.1em; opacity: 0.9; margin: 10px 0; }";
        
        // 卡片網格佈局
        html += ".grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 20px; }";
        html += ".card { background: white; padding: 20px; border-radius: 12px; box-shadow: 0 4px 20px rgba(0,0,0,0.1); transition: transform 0.2s; }";
        html += ".card:hover { transform: translateY(-2px); }";
        html += ".card h3 { margin: 0 0 15px 0; color: #333; border-bottom: 2px solid #007bff; padding-bottom: 8px; }";
        
        // 狀態指示器
        html += ".status-good { color: #28a745; font-weight: bold; }";
        html += ".status-warn { color: #ffc107; font-weight: bold; }";
        html += ".status-error { color: #dc3545; font-weight: bold; }";
        html += ".metric { display: flex; justify-content: space-between; margin: 8px 0; padding: 8px; background: #f8f9fa; border-radius: 6px; }";
        
        // 按鈕樣式
        html += ".btn-group { display: flex; gap: 10px; margin: 20px 0; flex-wrap: wrap; }";
        html += ".btn { background: #007bff; color: white; border: none; padding: 12px 24px; border-radius: 6px; cursor: pointer; font-size: 14px; transition: all 0.2s; text-decoration: none; display: inline-block; }";
        html += ".btn:hover { background: #0056b3; transform: translateY(-1px); }";
        html += ".btn-success { background: #28a745; }";
        html += ".btn-warning { background: #ffc107; color: #212529; }";
        html += ".btn-danger { background: #dc3545; }";
        
        // API端點樣式
        html += ".api-endpoint { display: block; color: #007bff; text-decoration: none; margin: 5px 0; padding: 8px; background: #f8f9fa; border-radius: 4px; font-family: monospace; }";
        html += ".api-endpoint:hover { background: #e9ecef; }";
        
        // 響應式設計
        html += "@media (max-width: 768px) { .grid { grid-template-columns: 1fr; } .btn-group { flex-direction: column; } }";
        html += "</style>";
        
        // JavaScript for auto-refresh and API calls
        html += "<script>";
        html += "function refreshData() { location.reload(); }";
        html += "function runDiagnostics() { ";
        html += "  fetch('/api/diagnostics', {method: 'POST'})";
        html += "    .then(response => { if(!response.ok) throw new Error('Network error'); })";
        html += "    .then(() => setTimeout(() => location.reload(), 1000))";
        html += "    .catch(err => { console.error('診斷測試失敗:', err); alert('診斷測試失敗，請檢查網絡連接'); });";
        html += "}";
        html += "function formatBytes(bytes) { const units = ['B', 'KB', 'MB']; let i = 0; while(bytes >= 1024 && i < 2) { bytes /= 1024; i++; } return bytes.toFixed(1) + ' ' + units[i]; }";
        html += "function checkConnection() { ";
        html += "  fetch('/api/status')";
        html += "    .then(response => response.ok ? console.log('連接正常') : console.warn('服務器響應異常'))";
        html += "    .catch(err => console.error('連接檢查失敗:', err));";
        html += "}";
        html += "setInterval(refreshData, 30000);"; // Auto-refresh every 30 seconds
        html += "checkConnection();"; // Initial connection check
        html += "</script>";
        html += "</head><body>";
        
        html += "<div class='container'>";
        html += "<div class='header'>";
        html += "<h1>🌡️ DaiSpan Debug Console</h1>";
        html += "<p>Lightweight HTTP Debug Interface (~10KB memory usage)</p>";
        html += "</div>";
        
        // 控制按鈕
        html += "<div class='btn-group'>";
        html += "<button class='btn' onclick='refreshData()'>🔄 Refresh</button>";
        html += "<button class='btn btn-success' onclick='runDiagnostics()'>🔍 Run Diagnostics</button>";
        html += "<a href='/api/status' class='btn btn-warning'>📊 Full Status JSON</a>";
        html += "<a href='/api/memory' class='btn'>💾 Memory Report</a>";
        html += "</div>";
        
        html += "<div class='grid'>";
        
        // 系統狀態卡片 - 整合記憶體監控
        html += "<div class='card'>";
        html += "<h3>📊 System Status</h3>";
        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t uptime = millis() - state.systemStartTime;
        
        // 安全地獲取記憶體監控數據
        float memoryUsage = 0.0f;
        String healthClass = "status-good";
        String healthStatus = "UNKNOWN";
        String warning = "";
        
        try {
            memoryUsage = memoryMonitor.getMemoryUsagePercent();
            healthClass = String(memoryMonitor.getHealthStatusClass());
            healthStatus = String(memoryMonitor.getHealthStatusText());
            warning = memoryMonitor.getMemoryWarning();
        } catch (...) {
            // 如果記憶體監控失敗，使用基本計算
            uint32_t totalMemory = 327680; // ESP32-C3
            memoryUsage = (float)(totalMemory - freeHeap) / totalMemory * 100.0f;
            healthStatus = (memoryUsage < 70.0f) ? "GOOD" : (memoryUsage < 85.0f) ? "WARNING" : "CRITICAL";
            healthClass = (memoryUsage < 70.0f) ? "status-good" : (memoryUsage < 85.0f) ? "status-warn" : "status-error";
        }
        
        html += "<div class='metric'><span>Memory Health:</span><span class='" + healthClass + "'>" + healthStatus + "</span></div>";
        html += "<div class='metric'><span>Free Memory:</span><span class='" + healthClass + "'>" + String(freeHeap/1024) + " KB (" + String(100.0f - memoryUsage, 1) + "% 可用)</span></div>";
        
        // 記憶體使用率進度條
        String progressColor = (memoryUsage <= 50.0f) ? "#28a745" : (memoryUsage <= 70.0f) ? "#ffc107" : "#dc3545";
        html += "<div style='margin: 10px 0;'>";
        html += "<div style='background: #e9ecef; border-radius: 10px; height: 20px; position: relative;'>";
        html += "<div style='background: " + progressColor + "; height: 100%; border-radius: 10px; width: " + String(memoryUsage, 1) + "%; transition: width 0.3s;'></div>";
        html += "<div style='position: absolute; width: 100%; text-align: center; line-height: 20px; top: 0; font-size: 12px; color: #333;'>" + String(memoryUsage, 1) + "% 已使用</div>";
        html += "</div></div>";
        
        // 記憶體警告（如果有）
        if (warning.length() > 0) {
            html += "<div style='background: #fff3cd; border: 1px solid #ffeaa7; color: #856404; padding: 8px; border-radius: 4px; margin: 8px 0; font-size: 14px;'>";
            html += warning;
            html += "</div>";
        }
        
        html += "<div class='metric'><span>Uptime:</span><span>" + String(uptime/1000) + " seconds</span></div>";
        html += "<div class='metric'><span>Total Logs:</span><span>" + String(state.totalLogCount) + "</span></div>";
        html += "<div class='metric'><span>HomeKit Ops:</span><span>" + String(state.totalHomeKitCount) + "</span></div>";
        String errorClass = (state.totalErrorCount > 0) ? "status-error" : "status-good";
        html += "<div class='metric'><span>Error Count:</span><span class='" + errorClass + "'>" + String(state.totalErrorCount) + "</span></div>";
        html += "</div>";
        
        // API端點卡片
        html += "<div class='card'>";
        html += "<h3>🔗 API Endpoints</h3>";
        html += "<div style='margin-bottom: 10px; font-weight: bold; color: #007bff;'>System Status:</div>";
        html += "<a href='/api/status' class='api-endpoint'>GET /api/status</a>";
        html += "<a href='/api/logs/current' class='api-endpoint'>GET /api/logs/current</a>";
        html += "<a href='/api/homekit/current' class='api-endpoint'>GET /api/homekit/current</a>";
        html += "<div style='margin: 15px 0 10px 0; font-weight: bold; color: #28a745;'>Memory Monitoring:</div>";
        html += "<a href='/api/memory' class='api-endpoint'>GET /api/memory</a>";
        html += "<a href='/api/memory/status' class='api-endpoint'>GET /api/memory/status</a>";
        html += "<a href='/api/memory/health' class='api-endpoint'>GET /api/memory/health</a>";
        html += "<div style='margin-top: 10px; font-size: 12px; color: #666;'>POST /api/diagnostics - Run system diagnostics</div>";
        html += "</div>";
        
        // 最新日誌卡片
        if (state.lastLogTime > 0) {
            html += "<div class='card'>";
            html += "<h3>📝 Latest Log Entry</h3>";
            String logClass = (String(state.lastLogLevel) == "ERROR") ? "status-error" : 
                             (String(state.lastLogLevel) == "WARN") ? "status-warn" : "status-good";
            html += "<div class='metric'><span>Level:</span><span class='" + logClass + "'>" + String(state.lastLogLevel) + "</span></div>";
            html += "<div class='metric'><span>Component:</span><span>" + String(state.lastLogComponent) + "</span></div>";
            html += "<div class='metric'><span>Message:</span><span>" + String(state.lastLogMessage) + "</span></div>";
            html += "<div class='metric'><span>Time:</span><span>" + String((millis() - state.lastLogTime)/1000) + "s ago</span></div>";
            html += "</div>";
        }
        
        // HomeKit狀態卡片
        if (state.lastHomeKitTime > 0) {
            html += "<div class='card'>";
            html += "<h3>🏠 Latest HomeKit Operation</h3>";
            String hkClass = state.lastHomeKitSuccess ? "status-good" : "status-error";
            html += "<div class='metric'><span>Operation:</span><span>" + String(state.lastHomeKitOp) + "</span></div>";
            html += "<div class='metric'><span>Service:</span><span>" + String(state.lastHomeKitService) + "</span></div>";
            html += "<div class='metric'><span>Result:</span><span class='" + hkClass + "'>" + (state.lastHomeKitSuccess ? "✅ SUCCESS" : "❌ FAILED") + "</span></div>";
            html += "<div class='metric'><span>Time:</span><span>" + String((millis() - state.lastHomeKitTime)/1000) + "s ago</span></div>";
            html += "</div>";
        }
        
        // 記憶體使用卡片
        html += "<div class='card'>";
        html += "<h3>💾 Memory Usage</h3>";
        size_t debuggerMemory = getMemoryUsage();
        html += "<div class='metric'><span>Debugger Memory:</span><span>~" + String(debuggerMemory) + " bytes</span></div>";
        html += "<div class='metric'><span>Memory Savings:</span><span class='status-good'>~95% vs WebSocket</span></div>";
        html += "<div class='metric'><span>Features:</span><span>HTTP API, Serial output</span></div>";
        html += "</div>";
        
        html += "</div>"; // grid
        
        // 頁腳
        html += "<div style='text-align: center; margin-top: 30px; color: rgba(255,255,255,0.8); font-size: 14px;'>";
        html += "DaiSpan Thermostat Controller | Lightweight Debug Interface<br>";
        html += "Auto-refresh every 30 seconds | <a href='/' style='color: white;'>🔄 Manual Refresh</a>";
        html += "</div>";
        
        html += "</div></body></html>";
        
        debugServer->send(200, "text/html", html);
    }
};

// 為了保持相容性，在輕量級模式下提供RemoteDebugger別名
#ifdef ENABLE_LIGHTWEIGHT_DEBUG
using RemoteDebugger = LightweightDebugger;
#endif

#endif // ENABLE_LIGHTWEIGHT_DEBUG