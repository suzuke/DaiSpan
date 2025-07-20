#pragma once

// 條件編譯：根據不同模式選擇調試實現
#if defined(ENABLE_LIGHTWEIGHT_DEBUG)

// 使用輕量級HTTP調試器 (節省90KB記憶體)
#include "LightweightDebugger.h"

#elif defined(ENABLE_REMOTE_DEBUG)

// 使用優化的WebSocket調試器 (節省65KB記憶體)
#include "OptimizedRemoteDebugger.h"

#else

// 遠端調試系統 (生產模式空實現 - 零記憶體開銷)
class RemoteDebugger {
private:
    RemoteDebugger() {}
    
public:
    // 單例模式 (空實現)
    static RemoteDebugger& getInstance() {
        static RemoteDebugger instance;
        return instance;
    }
    
    // 所有方法都是空實現 - 編譯器會優化掉
    bool begin(uint16_t port = 8081) { return true; }
    void loop() {}
    void stop() {}
    
    // 日誌功能 (空實現)
    void log(const String& level, const String& component, const String& message) {}
    void logHomeKitOperation(const String& operation, const String& service, 
                           const String& oldValue, const String& newValue, 
                           bool success, const String& errorMsg = "") {}
    
    // 狀態查詢 (空實現)
    String getSystemStatus() { return "{}"; }
    String getHomeKitStatus() { return "{}"; }
    String getOperationHistory() { return "{}"; }
    String getLogHistory() { return "{}"; }
    String getSerialLogHistory() { return "{}"; }
    
    // 遠端測試功能 (空實現)
    bool triggerThermostatTest(uint8_t mode, float temperature) { return false; }
    bool triggerFanTest(uint8_t speed) { return false; }
    bool runDiagnostics() { return false; }
    
    // 即時串口日誌轉發 (空實現)
    void logSerial(const String& message) {}
    void setSerialLogLevel(int level) {}
    
    // WebSocket 事件處理 (空實現)
    void onWebSocketEvent(uint8_t num, int type, uint8_t* payload, size_t length) {}
};

#endif // 調試模式選擇

// 便利宏定義 (在所有模式下都有效)
#if defined(ENABLE_REMOTE_DEBUG) || defined(ENABLE_LIGHTWEIGHT_DEBUG)
#define REMOTE_LOG_INFO(component, message) \
    RemoteDebugger::getInstance().log("INFO", component, message)

#define REMOTE_LOG_WARN(component, message) \
    RemoteDebugger::getInstance().log("WARN", component, message)

#define REMOTE_LOG_ERROR(component, message) \
    RemoteDebugger::getInstance().log("ERROR", component, message)

#define REMOTE_LOG_HOMEKIT_OP(operation, service, oldVal, newVal, success, error) \
    RemoteDebugger::getInstance().logHomeKitOperation(operation, service, oldVal, newVal, success, error)

#define REMOTE_WEBLOG(message) \
    RemoteDebugger::getInstance().logSerial(message)
#else
// 生產模式：空宏定義 - 完全無開銷
#define REMOTE_LOG_INFO(component, message)
#define REMOTE_LOG_WARN(component, message) 
#define REMOTE_LOG_ERROR(component, message)
#define REMOTE_LOG_HOMEKIT_OP(operation, service, oldVal, newVal, success, error)
#define REMOTE_WEBLOG(message)
#endif