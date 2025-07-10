#pragma once

#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <vector>
#include <string>

// 遠端調試系統
class RemoteDebugger {
private:
    WebSocketsServer* wsServer;
    std::vector<uint8_t> connectedClients;
    bool debugEnabled;
    
    // 日誌緩存
    std::vector<String> logBuffer;
    static const size_t MAX_LOG_BUFFER = 100;
    
    // HomeKit 狀態追蹤
    struct HomeKitOperation {
        unsigned long timestamp;
        String operation;
        String service;
        String oldValue;
        String newValue;
        bool success;
        String errorMsg;
    };
    
    std::vector<HomeKitOperation> operationHistory;
    static const size_t MAX_OPERATION_HISTORY = 50;
    
    // 串口日誌轉發
    bool serialLogEnabled;
    int serialLogLevel;
    std::vector<String> serialLogBuffer;
    static const size_t MAX_SERIAL_LOG_BUFFER = 200;
    
public:
    // 單例模式
    static RemoteDebugger& getInstance();
    
    // 初始化和控制
    bool begin(uint16_t port = 8081);
    void loop();
    void stop();
    
    // 日誌功能
    void log(const String& level, const String& component, const String& message);
    void logHomeKitOperation(const String& operation, const String& service, 
                           const String& oldValue, const String& newValue, 
                           bool success, const String& errorMsg = "");
    
    // 狀態查詢
    String getSystemStatus();
    String getHomeKitStatus();
    String getOperationHistory();
    String getLogHistory();
    String getSerialLogHistory();
    
    // 遠端測試功能
    bool triggerThermostatTest(uint8_t mode, float temperature);
    bool triggerFanTest(uint8_t speed);
    bool runDiagnostics();
    
    // 即時串口日誌轉發
    void logSerial(const String& message);
    void setSerialLogLevel(int level);
    
    // WebSocket 事件處理
    void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
    
private:
    RemoteDebugger() : wsServer(nullptr), debugEnabled(false), serialLogEnabled(true), serialLogLevel(2) {}
    void broadcastMessage(const String& message);
    void sendToClient(uint8_t clientId, const String& message);
    String createJsonMessage(const String& type, const String& data);
};

// 便利宏定義
#define REMOTE_LOG_INFO(component, message) \
    RemoteDebugger::getInstance().log("INFO", component, message)

#define REMOTE_LOG_WARN(component, message) \
    RemoteDebugger::getInstance().log("WARN", component, message)

#define REMOTE_LOG_ERROR(component, message) \
    RemoteDebugger::getInstance().log("ERROR", component, message)

#define REMOTE_LOG_HOMEKIT_OP(operation, service, oldVal, newVal, success, error) \
    RemoteDebugger::getInstance().logHomeKitOperation(operation, service, oldVal, newVal, success, error)

// 串口日誌轉發宏
#define REMOTE_WEBLOG(message) \
    RemoteDebugger::getInstance().logSerial(message)