#pragma once

#ifdef ENABLE_REMOTE_DEBUG

#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <array>
#include "MemoryMonitor.h"

// 優化的遠端調試系統 (節省65KB記憶體)
class OptimizedRemoteDebugger {
private:
    // 循環緩衝區模板 - 固定記憶體分配
    template<typename T, size_t N>
    class CircularBuffer {
    private:
        std::array<T, N> buffer;
        size_t head = 0;
        size_t tail = 0;
        size_t count = 0;
        
    public:
        void push(const T& item) {
            buffer[head] = item;
            head = (head + 1) % N;
            if (count < N) {
                count++;
            } else {
                tail = (tail + 1) % N;
            }
        }
        
        bool empty() const { return count == 0; }
        size_t size() const { return count; }
        size_t capacity() const { return N; }
        
        // 獲取最新的條目
        const T& latest() const {
            if (count == 0) return buffer[0];
            size_t latest_idx = (head == 0) ? N - 1 : head - 1;
            return buffer[latest_idx];
        }
        
        // 遍歷所有有效條目 (從最舊到最新)
        void forEach(std::function<void(const T&)> func) const {
            if (count == 0) return;
            
            for (size_t i = 0; i < count; i++) {
                size_t idx = (tail + i) % N;
                func(buffer[idx]);
            }
        }
        
        void clear() {
            head = tail = count = 0;
        }
    };
    
    // 優化的日誌條目 - 使用固定大小字符數組
    struct CompactLogEntry {
        uint32_t timestamp;
        char level[8];       // "INFO", "WARN", "ERROR"
        char component[16];  // 組件名稱
        char message[96];    // 日誌訊息
        // 總計: 124字節 vs 原本~420字節 (減少70%)
        
        CompactLogEntry() {
            timestamp = 0;
            level[0] = '\0';
            component[0] = '\0';
            message[0] = '\0';
        }
        
        CompactLogEntry(const String& lvl, const String& comp, const String& msg) {
            timestamp = millis();
            strncpy(level, lvl.c_str(), sizeof(level) - 1);
            strncpy(component, comp.c_str(), sizeof(component) - 1);
            strncpy(message, msg.c_str(), sizeof(message) - 1);
            level[sizeof(level) - 1] = '\0';
            component[sizeof(component) - 1] = '\0';
            message[sizeof(message) - 1] = '\0';
        }
    };
    
    // 優化的HomeKit操作記錄
    struct CompactHomeKitOperation {
        uint32_t timestamp;
        char operation[24];   // 操作類型
        char service[16];     // 服務名稱  
        char oldValue[24];    // 舊值
        char newValue[24];    // 新值
        bool success;
        char errorMsg[48];    // 錯誤訊息
        // 總計: 141字節 vs 原本849字節 (減少83%)
        
        CompactHomeKitOperation() {
            timestamp = 0;
            operation[0] = '\0';
            service[0] = '\0';
            oldValue[0] = '\0';
            newValue[0] = '\0';
            success = false;
            errorMsg[0] = '\0';
        }
        
        CompactHomeKitOperation(const String& op, const String& svc, 
                              const String& oldVal, const String& newVal, 
                              bool succ, const String& error) {
            timestamp = millis();
            strncpy(operation, op.c_str(), sizeof(operation) - 1);
            strncpy(service, svc.c_str(), sizeof(service) - 1);
            strncpy(oldValue, oldVal.c_str(), sizeof(oldValue) - 1);
            strncpy(newValue, newVal.c_str(), sizeof(newValue) - 1);
            success = succ;
            strncpy(errorMsg, error.c_str(), sizeof(errorMsg) - 1);
            operation[sizeof(operation) - 1] = '\0';
            service[sizeof(service) - 1] = '\0';
            oldValue[sizeof(oldValue) - 1] = '\0';
            newValue[sizeof(newValue) - 1] = '\0';
            errorMsg[sizeof(errorMsg) - 1] = '\0';
        }
    };
    
    // 串口日誌條目
    struct CompactSerialEntry {
        uint32_t timestamp;
        char message[80];    // 串口訊息
        // 總計: 84字節 vs 原本~140字節 (減少40%)
        
        CompactSerialEntry() {
            timestamp = 0;
            message[0] = '\0';
        }
        
        CompactSerialEntry(const String& msg) {
            timestamp = millis();
            strncpy(message, msg.c_str(), sizeof(message) - 1);
            message[sizeof(message) - 1] = '\0';
        }
    };
    
    // 循環緩衝區實例 - 大幅減少記憶體使用
    CircularBuffer<CompactLogEntry, 50> logBuffer;           // 50 × 124 = 6.2KB
    CircularBuffer<CompactHomeKitOperation, 20> operationHistory; // 20 × 141 = 2.8KB
    CircularBuffer<CompactSerialEntry, 100> serialLogBuffer;     // 100 × 84 = 8.4KB
    // 總記憶體: ~17.4KB vs 原本95KB (減少82%)
    
    WebSocketsServer* wsServer;
    std::vector<uint8_t> connectedClients;
    bool debugEnabled;
    bool serialLogEnabled;
    int serialLogLevel;
    
    // 記憶體監控
    MemoryMonitor memoryMonitor;
    
public:
    // 單例模式
    static OptimizedRemoteDebugger& getInstance();
    
    // 初始化和控制
    bool begin(uint16_t port = 8081);
    void loop();
    void stop();
    
    // 日誌功能 - 使用優化的數據結構
    void log(const String& level, const String& component, const String& message);
    
    void logHomeKitOperation(const String& operation, const String& service, 
                           const String& oldValue, const String& newValue, 
                           bool success, const String& errorMsg = "");
    
    // 狀態查詢 - 返回緊湊的JSON
    String getSystemStatus();
    
    String getHomeKitStatus();
    
    String getLogHistory();
    
    // 串口日誌轉發
    void logSerial(const String& message);
    
    // 記憶體使用統計
    size_t getMemoryUsage() const;
    
    String getMemoryReport() const;
    
    // 簡化的測試功能
    bool triggerThermostatTest(uint8_t mode, float temperature);
    bool triggerFanTest(uint8_t speed);
    bool runDiagnostics();
    
    // 記憶體監控功能
    String getDetailedMemoryStatus();
    String getMemoryHealthJson();
    bool isMemoryHealthy();
    void updateMemoryMonitoring();

private:
    OptimizedRemoteDebugger() : wsServer(nullptr), debugEnabled(false), serialLogEnabled(true), serialLogLevel(2) {}
    
    void broadcastMessage(const String& message);
    String createJsonMessage(const String& type, const String& data);
    void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
    void handleClientCommand(uint8_t clientId, const String& command);
};

// 為了保持相容性，提供原始RemoteDebugger的別名
using RemoteDebugger = OptimizedRemoteDebugger;

#endif // ENABLE_REMOTE_DEBUG