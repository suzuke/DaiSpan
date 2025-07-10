#include "common/RemoteDebugger.h"
#include "common/Debug.h"
#include "controller/IThermostatControl.h"
#include "device/ThermostatDevice.h"
#include "device/FanDevice.h"
#include <Arduino.h>
#include <WiFi.h>

// 全域變數引用（需要在 main.cpp 中定義）
extern IThermostatControl* thermostatController;
extern ThermostatDevice* thermostatDevice;
extern FanDevice* fanDevice;
extern bool homeKitInitialized;
extern bool deviceInitialized;

// Debug.h 中的函數實作
void remoteWebLog(const String& message) {
    RemoteDebugger::getInstance().logSerial(message);
}

RemoteDebugger& RemoteDebugger::getInstance() {
    static RemoteDebugger instance;
    return instance;
}

bool RemoteDebugger::begin(uint16_t port) {
    if (wsServer) {
        DEBUG_INFO_PRINT("[RemoteDebug] WebSocket 伺服器已在運行\n");
        return true;
    }
    
    DEBUG_INFO_PRINT("[RemoteDebug] 啟動遠端調試系統在端口 %d\n", port);
    
    // 檢查記憶體
    if (ESP.getFreeHeap() < 50000) {
        DEBUG_ERROR_PRINT("[RemoteDebug] 記憶體不足，無法啟動調試系統\n");
        return false;
    }
    
    wsServer = new WebSocketsServer(port);
    if (!wsServer) {
        DEBUG_ERROR_PRINT("[RemoteDebug] WebSocket 伺服器創建失敗\n");
        return false;
    }
    
    // 設置 WebSocket 事件處理器
    wsServer->onEvent([this](uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
        this->onWebSocketEvent(num, type, payload, length);
    });
    
    wsServer->begin();
    debugEnabled = true;
    
    // 發送啟動日誌
    log("INFO", "RemoteDebug", "遠端調試系統已啟動");
    
    DEBUG_INFO_PRINT("[RemoteDebug] 遠端調試系統啟動成功\n");
    return true;
}

void RemoteDebugger::loop() {
    if (wsServer && debugEnabled) {
        wsServer->loop();
    }
}

void RemoteDebugger::stop() {
    if (wsServer) {
        delete wsServer;
        wsServer = nullptr;
    }
    debugEnabled = false;
    connectedClients.clear();
    DEBUG_INFO_PRINT("[RemoteDebug] 遠端調試系統已停止\n");
}

void RemoteDebugger::log(const String& level, const String& component, const String& message) {
    if (!debugEnabled) return;
    
    // 創建日誌條目
    String timestamp = String(millis());
    String logEntry = "[" + timestamp + "] [" + level + "] [" + component + "] " + message;
    
    // 添加到緩存
    logBuffer.push_back(logEntry);
    if (logBuffer.size() > MAX_LOG_BUFFER) {
        logBuffer.erase(logBuffer.begin());
    }
    
    // 廣播給所有連接的客戶端
    String jsonMsg = createJsonMessage("log", logEntry);
    broadcastMessage(jsonMsg);
}

void RemoteDebugger::logHomeKitOperation(const String& operation, const String& service, 
                                       const String& oldValue, const String& newValue, 
                                       bool success, const String& errorMsg) {
    if (!debugEnabled) return;
    
    HomeKitOperation op;
    op.timestamp = millis();
    op.operation = operation;
    op.service = service;
    op.oldValue = oldValue;
    op.newValue = newValue;
    op.success = success;
    op.errorMsg = errorMsg;
    
    // 添加到歷史記錄
    operationHistory.push_back(op);
    if (operationHistory.size() > MAX_OPERATION_HISTORY) {
        operationHistory.erase(operationHistory.begin());
    }
    
    // 創建 JSON 訊息
    JsonDocument doc;
    doc["type"] = "homekit_operation";
    doc["timestamp"] = op.timestamp;
    doc["operation"] = op.operation;
    doc["service"] = op.service;
    doc["old_value"] = op.oldValue;
    doc["new_value"] = op.newValue;
    doc["success"] = op.success;
    doc["error"] = op.errorMsg;
    
    String jsonString;
    serializeJson(doc, jsonString);
    broadcastMessage(jsonString);
    
    // 同時記錄到標準日誌
    String logMsg = operation + " [" + service + "] " + oldValue + " -> " + newValue + 
                   (success ? " ✓" : " ✗ " + errorMsg);
    log(success ? "INFO" : "ERROR", "HomeKit", logMsg);
}

String RemoteDebugger::getSystemStatus() {
    JsonDocument doc;
    doc["type"] = "system_status";
    doc["timestamp"] = millis();
    doc["uptime"] = millis();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["homekit_initialized"] = homeKitInitialized;
    doc["device_initialized"] = deviceInitialized;
    doc["debug_clients"] = connectedClients.size();
    
    String result;
    serializeJson(doc, result);
    return result;
}

String RemoteDebugger::getHomeKitStatus() {
    JsonDocument doc;
    doc["type"] = "homekit_status";
    doc["timestamp"] = millis();
    
    if (thermostatController) {
        doc["thermostat"]["power"] = thermostatController->getPower();
        doc["thermostat"]["mode"] = thermostatController->getTargetMode();
        doc["thermostat"]["target_temp"] = thermostatController->getTargetTemperature();
        doc["thermostat"]["current_temp"] = thermostatController->getCurrentTemperature();
        doc["thermostat"]["fan_speed"] = thermostatController->getFanSpeed();
    } else {
        doc["thermostat"] = nullptr;
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}

String RemoteDebugger::getOperationHistory() {
    JsonDocument doc;
    doc["type"] = "operation_history";
    doc["timestamp"] = millis();
    
    JsonArray operations = doc["operations"].to<JsonArray>();
    for (const auto& op : operationHistory) {
        JsonObject opObj = operations.add<JsonObject>();
        opObj["timestamp"] = op.timestamp;
        opObj["operation"] = op.operation;
        opObj["service"] = op.service;
        opObj["old_value"] = op.oldValue;
        opObj["new_value"] = op.newValue;
        opObj["success"] = op.success;
        opObj["error"] = op.errorMsg;
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}

String RemoteDebugger::getLogHistory() {
    JsonDocument doc;
    doc["type"] = "log_history";
    doc["timestamp"] = millis();
    
    JsonArray logs = doc["logs"].to<JsonArray>();
    for (const auto& logEntry : logBuffer) {
        logs.add(logEntry);
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}

bool RemoteDebugger::triggerThermostatTest(uint8_t mode, float temperature) {
    if (!thermostatController) {
        log("ERROR", "RemoteTest", "恆溫器控制器不可用");
        return false;
    }
    
    log("INFO", "RemoteTest", "開始恆溫器測試 - 模式:" + String(mode) + " 溫度:" + String(temperature));
    
    bool success = true;
    
    // 測試模式設置
    if (!thermostatController->setTargetMode(mode)) {
        log("ERROR", "RemoteTest", "模式設置失敗");
        success = false;
    }
    
    // 測試溫度設置
    if (!thermostatController->setTargetTemperature(temperature)) {
        log("ERROR", "RemoteTest", "溫度設置失敗");
        success = false;
    }
    
    if (success) {
        log("INFO", "RemoteTest", "恆溫器測試完成 ✓");
    } else {
        log("ERROR", "RemoteTest", "恆溫器測試失敗 ✗");
    }
    
    return success;
}

bool RemoteDebugger::triggerFanTest(uint8_t speed) {
    if (!thermostatController) {
        log("ERROR", "RemoteTest", "風扇控制器不可用");
        return false;
    }
    
    log("INFO", "RemoteTest", "開始風扇測試 - 速度:" + String(speed));
    
    bool success = thermostatController->setFanSpeed(speed);
    
    if (success) {
        log("INFO", "RemoteTest", "風扇測試完成 ✓");
    } else {
        log("ERROR", "RemoteTest", "風扇測試失敗 ✗");
    }
    
    return success;
}

bool RemoteDebugger::runDiagnostics() {
    log("INFO", "Diagnostics", "開始系統診斷...");
    
    JsonDocument report;
    report["type"] = "diagnostics_report";
    report["timestamp"] = millis();
    
    // 記憶體檢查
    uint32_t freeHeap = ESP.getFreeHeap();
    report["memory"]["free_heap"] = freeHeap;
    report["memory"]["status"] = (freeHeap > 50000) ? "OK" : "LOW";
    
    // WiFi 檢查
    bool wifiOk = (WiFi.status() == WL_CONNECTED);
    report["wifi"]["connected"] = wifiOk;
    report["wifi"]["rssi"] = WiFi.RSSI();
    report["wifi"]["status"] = wifiOk ? "OK" : "DISCONNECTED";
    
    // HomeKit 檢查
    report["homekit"]["initialized"] = homeKitInitialized;
    report["homekit"]["device_available"] = (thermostatController != nullptr);
    report["homekit"]["status"] = (homeKitInitialized && thermostatController) ? "OK" : "ERROR";
    
    // 控制器檢查
    if (thermostatController) {
        report["controller"]["type"] = "available";
        report["controller"]["power"] = thermostatController->getPower();
        report["controller"]["status"] = "OK";
    } else {
        report["controller"]["type"] = "unavailable";
        report["controller"]["status"] = "ERROR";
    }
    
    String reportStr;
    serializeJson(report, reportStr);
    broadcastMessage(reportStr);
    
    log("INFO", "Diagnostics", "系統診斷完成");
    return true;
}

void RemoteDebugger::logSerial(const String& message) {
    if (!serialLogEnabled || !debugEnabled) return;
    
    // 添加時間戳記
    String timestampedMsg = "[" + String(millis()) + "] " + message;
    
    // 添加到串口日誌緩存
    serialLogBuffer.push_back(timestampedMsg);
    if (serialLogBuffer.size() > MAX_SERIAL_LOG_BUFFER) {
        serialLogBuffer.erase(serialLogBuffer.begin());
    }
    
    // 即時廣播給所有連接的客戶端
    String jsonMsg = createJsonMessage("serial_log", timestampedMsg);
    broadcastMessage(jsonMsg);
}

void RemoteDebugger::setSerialLogLevel(int level) {
    serialLogLevel = level;
    log("INFO", "RemoteDebug", "串口日誌級別設定為: " + String(level));
}

String RemoteDebugger::getSerialLogHistory() {
    JsonDocument doc;
    doc["type"] = "serial_log_history";
    doc["timestamp"] = millis();
    
    JsonArray logs = doc["logs"].to<JsonArray>();
    for (const auto& logEntry : serialLogBuffer) {
        logs.add(logEntry);
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}

void RemoteDebugger::onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            DEBUG_INFO_PRINT("[RemoteDebug] 客戶端 %u 斷開連接\n", num);
            connectedClients.erase(std::remove(connectedClients.begin(), connectedClients.end(), num), connectedClients.end());
            break;
            
        case WStype_CONNECTED:
            {
                IPAddress ip = wsServer->remoteIP(num);
                DEBUG_INFO_PRINT("[RemoteDebug] 客戶端 %u 連接: %s\n", num, ip.toString().c_str());
                connectedClients.push_back(num);
                
                // 發送歡迎訊息和當前狀態
                sendToClient(num, getSystemStatus());
                sendToClient(num, getHomeKitStatus());
                sendToClient(num, getSerialLogHistory());
            }
            break;
            
        case WStype_TEXT:
            {
                String message = String((char*)payload);
                DEBUG_INFO_PRINT("[RemoteDebug] 收到訊息: %s\n", message.c_str());
                
                // 解析 JSON 命令
                JsonDocument doc;
                if (deserializeJson(doc, message) == DeserializationError::Ok) {
                    String command = doc["command"];
                    
                    if (command == "get_status") {
                        sendToClient(num, getSystemStatus());
                        sendToClient(num, getHomeKitStatus());
                    } else if (command == "get_history") {
                        sendToClient(num, getOperationHistory());
                        sendToClient(num, getLogHistory());
                    } else if (command == "test_thermostat") {
                        uint8_t mode = doc["mode"];
                        float temp = doc["temperature"];
                        triggerThermostatTest(mode, temp);
                    } else if (command == "test_fan") {
                        uint8_t speed = doc["speed"];
                        triggerFanTest(speed);
                    } else if (command == "diagnostics") {
                        runDiagnostics();
                    } else if (command == "get_serial_logs") {
                        sendToClient(num, getSerialLogHistory());
                    } else if (command == "set_serial_log_level") {
                        int level = doc["level"];
                        setSerialLogLevel(level);
                    }
                }
            }
            break;
            
        default:
            break;
    }
}

void RemoteDebugger::broadcastMessage(const String& message) {
    if (!wsServer || !debugEnabled) return;
    
    for (uint8_t clientId : connectedClients) {
        String msg = message; // 創建非const副本
        wsServer->sendTXT(clientId, msg);
    }
}

void RemoteDebugger::sendToClient(uint8_t clientId, const String& message) {
    if (!wsServer || !debugEnabled) return;
    
    String msg = message; // 創建非const副本
    wsServer->sendTXT(clientId, msg);
}

String RemoteDebugger::createJsonMessage(const String& type, const String& data) {
    JsonDocument doc;
    doc["type"] = type;
    doc["timestamp"] = millis();
    doc["data"] = data;
    
    String result;
    serializeJson(doc, result);
    return result;
}