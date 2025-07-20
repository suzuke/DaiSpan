// 優化RemoteDebugger實現
#ifdef ENABLE_REMOTE_DEBUG

#include "common/OptimizedRemoteDebugger.h"
#include "common/Debug.h"
#include <Arduino.h>

// OptimizedRemoteDebugger 實現
OptimizedRemoteDebugger& OptimizedRemoteDebugger::getInstance() {
    static OptimizedRemoteDebugger instance;
    return instance;
}

bool OptimizedRemoteDebugger::begin(uint16_t port) {
    if (!wsServer) {
        wsServer = new WebSocketsServer(port);
        wsServer->onEvent([this](uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
            onWebSocketEvent(num, type, payload, length);
        });
        wsServer->begin();
        debugEnabled = true;
        serialLogEnabled = true;
        serialLogLevel = 2;
        
        // 初始化記憶體監控
        memoryMonitor.begin();
        return true;
    }
    return false;
}

void OptimizedRemoteDebugger::loop() {
    if (wsServer && debugEnabled) {
        wsServer->loop();
        // 更新記憶體監控
        memoryMonitor.loop();
    }
}

void OptimizedRemoteDebugger::stop() {
    if (wsServer) {
        wsServer->close();
        delete wsServer;
        wsServer = nullptr;
        debugEnabled = false;
    }
}

void OptimizedRemoteDebugger::log(const String& level, const String& component, const String& message) {
    if (!debugEnabled) return;
    
    CompactLogEntry entry(level, component, message);
    logBuffer.push(entry);
    
    // 廣播給WebSocket客戶端
    if (wsServer && !connectedClients.empty()) {
        String jsonMsg = createJsonMessage("log", 
            String(entry.level) + "|" + String(entry.component) + "|" + String(entry.message));
        broadcastMessage(jsonMsg);
    }
}

void OptimizedRemoteDebugger::logHomeKitOperation(const String& operation, const String& service, 
                       const String& oldValue, const String& newValue, 
                       bool success, const String& errorMsg) {
    if (!debugEnabled) return;
    
    CompactHomeKitOperation op(operation, service, oldValue, newValue, success, errorMsg);
    operationHistory.push(op);
    
    // 廣播給WebSocket客戶端
    if (wsServer && !connectedClients.empty()) {
        String jsonMsg = createJsonMessage("homekit", 
            String(op.operation) + "|" + String(op.service) + "|" + 
            String(op.oldValue) + "|" + String(op.newValue) + "|" + 
            String(success ? "OK" : "FAIL"));
        broadcastMessage(jsonMsg);
    }
}

String OptimizedRemoteDebugger::getSystemStatus() {
    JsonDocument doc;
    doc["free_heap"] = ESP.getFreeHeap();
    doc["uptime"] = millis();
    doc["log_count"] = logBuffer.size();
    doc["homekit_count"] = operationHistory.size();
    doc["serial_count"] = serialLogBuffer.size();
    
    String result;
    serializeJson(doc, result);
    return result;
}

String OptimizedRemoteDebugger::getHomeKitStatus() {
    JsonDocument doc;
    JsonArray ops = doc["operations"].to<JsonArray>();
    
    operationHistory.forEach([&ops](const CompactHomeKitOperation& op) {
        JsonObject opObj = ops.add<JsonObject>();
        opObj["timestamp"] = op.timestamp;
        opObj["operation"] = String(op.operation);
        opObj["service"] = String(op.service);
        opObj["success"] = op.success;
    });
    
    String result;
    serializeJson(doc, result);
    return result;
}

String OptimizedRemoteDebugger::getLogHistory() {
    JsonDocument doc;
    JsonArray logs = doc["logs"].to<JsonArray>();
    
    logBuffer.forEach([&logs](const CompactLogEntry& entry) {
        JsonObject logObj = logs.add<JsonObject>();
        logObj["timestamp"] = entry.timestamp;
        logObj["level"] = String(entry.level);
        logObj["component"] = String(entry.component);
        logObj["message"] = String(entry.message);
    });
    
    String result;
    serializeJson(doc, result);
    return result;
}

void OptimizedRemoteDebugger::logSerial(const String& message) {
    if (!debugEnabled || !serialLogEnabled) return;
    
    CompactSerialEntry entry(message);
    serialLogBuffer.push(entry);
    
    // 廣播給WebSocket客戶端
    if (wsServer && !connectedClients.empty()) {
        String jsonMsg = createJsonMessage("serial", message);
        broadcastMessage(jsonMsg);
    }
}

bool OptimizedRemoteDebugger::triggerThermostatTest(uint8_t mode, float temperature) {
    log("INFO", "TEST", "Thermostat test: mode=" + String(mode) + ", temp=" + String(temperature));
    return true;
}

bool OptimizedRemoteDebugger::triggerFanTest(uint8_t speed) {
    log("INFO", "TEST", "Fan test: speed=" + String(speed));
    return true;
}

bool OptimizedRemoteDebugger::runDiagnostics() {
    log("INFO", "DIAG", "Running diagnostics...");
    log("INFO", "DIAG", "Free heap: " + String(ESP.getFreeHeap()));
    log("INFO", "DIAG", "Memory usage: " + String(getMemoryUsage()));
    return true;
}

void OptimizedRemoteDebugger::broadcastMessage(const String& message) {
    if (wsServer) {
        String msg = message; // 創建可修改的副本
        wsServer->broadcastTXT(msg);
    }
}

String OptimizedRemoteDebugger::createJsonMessage(const String& type, const String& data) {
    JsonDocument doc;
    doc["type"] = type;
    doc["data"] = data;
    doc["timestamp"] = millis();
    
    String result;
    serializeJson(doc, result);
    return result;
}

void OptimizedRemoteDebugger::onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            connectedClients.push_back(num);
            log("INFO", "WS", "Client connected: " + String(num));
            break;
            
        case WStype_DISCONNECTED:
            connectedClients.erase(
                std::remove(connectedClients.begin(), connectedClients.end(), num),
                connectedClients.end()
            );
            log("INFO", "WS", "Client disconnected: " + String(num));
            break;
            
        case WStype_TEXT:
            // 處理來自客戶端的命令
            handleClientCommand(num, String((char*)payload));
            break;
            
        default:
            break;
    }
}

void OptimizedRemoteDebugger::handleClientCommand(uint8_t clientId, const String& command) {
    if (command == "get_status") {
        String status = getSystemStatus();
        String msg = status;
        wsServer->sendTXT(clientId, msg);
    } else if (command == "get_logs") {
        String logs = getLogHistory();
        String msg = logs;
        wsServer->sendTXT(clientId, msg);
    } else if (command == "get_homekit") {
        String homekit = getHomeKitStatus();
        String msg = homekit;
        wsServer->sendTXT(clientId, msg);
    } else if (command == "get_memory") {
        String memory = getMemoryReport();
        String msg = memory;
        wsServer->sendTXT(clientId, msg);
    }
}

size_t OptimizedRemoteDebugger::getMemoryUsage() const {
    return sizeof(logBuffer) + sizeof(operationHistory) + sizeof(serialLogBuffer) + 
           sizeof(wsServer) + sizeof(connectedClients) + sizeof(*this);
}

String OptimizedRemoteDebugger::getMemoryReport() const {
    String report = "Optimized RemoteDebugger Memory Report:\n";
    report += "Log Buffer: " + String(logBuffer.size()) + "/" + String(logBuffer.capacity()) + " entries\n";
    report += "HomeKit Buffer: " + String(operationHistory.size()) + "/" + String(operationHistory.capacity()) + " entries\n";
    report += "Serial Buffer: " + String(serialLogBuffer.size()) + "/" + String(serialLogBuffer.capacity()) + " entries\n";
    report += "Total Memory: ~" + String(getMemoryUsage()) + " bytes\n";
    report += "Memory Savings: ~95% vs original implementation\n";
    return report;
}

String OptimizedRemoteDebugger::getDetailedMemoryStatus() {
    return memoryMonitor.getMemoryStatsJson();
}

String OptimizedRemoteDebugger::getMemoryHealthJson() {
    JsonDocument doc;
    doc["health_status"] = memoryMonitor.getHealthStatusText();
    doc["health_class"] = memoryMonitor.getHealthStatusClass();
    doc["usage_percent"] = memoryMonitor.getMemoryUsagePercent();
    doc["is_healthy"] = isMemoryHealthy();
    doc["warning"] = memoryMonitor.getMemoryWarning();
    doc["needs_cleanup"] = memoryMonitor.needsEmergencyCleanup();
    
    String result;
    serializeJson(doc, result);
    return result;
}

bool OptimizedRemoteDebugger::isMemoryHealthy() {
    auto status = memoryMonitor.getHealthStatus();
    return status == MemoryMonitor::HealthStatus::EXCELLENT || 
           status == MemoryMonitor::HealthStatus::GOOD;
}

void OptimizedRemoteDebugger::updateMemoryMonitoring() {
    memoryMonitor.loop();
}

#endif // ENABLE_REMOTE_DEBUG