#pragma once

#include "IS21Protocol.h"
#include "S21ProtocolVariants.h"
#include "../common/Debug.h"
#include "../common/ThermostatMode.h"

// Some common S21 definitions
#define	STX	2
#define	ETX	3
#define	ACK	6
#define	NAK	21

// Packet structure
#define S21_STX_OFFSET     0
#define S21_CMD0_OFFSET    1
#define S21_CMD1_OFFSET    2
#define S21_PAYLOAD_OFFSET 3

// Length of a framing (STX + CRC + ETX)
#define S21_FRAMING_LEN 3
// A typical payload length, but there are deviations
#define S21_PAYLOAD_LEN 4
// A minimum length of a packet (with no payload): framing + CMD0 + CMD1
#define S21_MIN_PKT_LEN (S21_FRAMING_LEN + 2)

// v3 packets use 4-character command codes
#define S21_V3_CMD2_OFFSET 3
#define S21_V3_CMD3_OFFSET 4
#define S21_V3_PAYLOAD_OFFSET 5
#define S21_MIN_V3_PKT_LEN (S21_FRAMING_LEN + 4)

// Encoding for minimum target temperature value, correspond to 18 deg.C.
#define AC_MIN_TEMP_VALUE '@'

// FX 擴展命令定義 (基於 Faikin 規範)
#define FX_COMMAND_COUNT 16
#define FX_MAX_PAYLOAD_LEN 8
#define FX_MAX_RESPONSE_LEN 16

// FX 命令類型枚舉
enum class FXCommandType {
    FX00 = 0x00,  // 基本擴展查詢
    FX10 = 0x10,  // 感測器擴展
    FX20 = 0x20,  // 控制擴展
    FX30 = 0x30,  // 狀態擴展
    FX40 = 0x40,  // 診斷擴展
    FX50 = 0x50,  // 維護擴展
    FX60 = 0x60,  // 能耗擴展
    FX70 = 0x70,  // 系統擴展
    FX80 = 0x80,  // 網路擴展
    FX90 = 0x90,  // 安全擴展
    FXA0 = 0xA0,  // 製造商擴展A
    FXB0 = 0xB0,  // 製造商擴展B
    FXC0 = 0xC0,  // 地區擴展
    FXD0 = 0xD0,  // 型號擴展
    FXE0 = 0xE0,  // 版本擴展
    FXF0 = 0xF0   // 未來擴展
};

// FX 回應數據結構
struct FXResponse {
    FXCommandType commandType;
    uint8_t subCommand;
    uint8_t dataLength;
    uint8_t data[FX_MAX_RESPONSE_LEN];
    bool isValid;
    
    FXResponse() : commandType(FXCommandType::FX00), subCommand(0), 
                   dataLength(0), isValid(false) {
        memset(data, 0, sizeof(data));
    }
};

class S21Protocol : public IS21Protocol {
private:
    HardwareSerial& serial;
    S21ProtocolVersion protocolVersion;
    S21Features features;
    S21Status status;  // 新增狀態追蹤
    bool isInitialized;
    
    // FX 命令支援追蹤
    bool fxCommandSupported[FX_COMMAND_COUNT];
    uint32_t supportedCommandsBitmap;  // 位圖記錄支援的基本命令
    bool dynamicDiscoveryCompleted;
    
    // 協議變體支援 (Phase 3)
    S21ProtocolVariant currentVariant;
    S21ProtocolVariantDetector variantDetector;
    S21ProtocolVariantAdapter* variantAdapter;
    
    // 通訊品質監控 (Phase 4)
    struct CommunicationQuality {
        float avgResponseTime;        // 平均回應時間（毫秒）
        float maxResponseTime;        // 最大回應時間
        float minResponseTime;        // 最小回應時間
        uint32_t timeoutCount;        // 超時次數
        uint32_t checksumErrorCount;  // 校驗和錯誤次數
        uint32_t totalCommands;       // 總命令數
        float qualityScore;           // 品質分數 (0-100)
        unsigned long lastUpdateTime; // 最後更新時間
        bool isStable;                // 連接是否穩定
        
        CommunicationQuality() : avgResponseTime(0), maxResponseTime(0), 
                               minResponseTime(999999), timeoutCount(0),
                               checksumErrorCount(0), totalCommands(0),
                               qualityScore(100), lastUpdateTime(0), isStable(true) {}
    } commQuality;
    
    // 錯誤恢復機制
    struct ErrorRecovery {
        uint8_t consecutiveErrors;    // 連續錯誤計數
        uint8_t recoveryAttempts;     // 恢復嘗試次數
        unsigned long lastRecoveryTime; // 最後恢復時間
        bool inRecoveryMode;          // 是否處於恢復模式
        uint32_t adaptiveTimeout;     // 自適應超時值
        
        ErrorRecovery() : consecutiveErrors(0), recoveryAttempts(0),
                         lastRecoveryTime(0), inRecoveryMode(false),
                         adaptiveTimeout(1000) {}
    } errorRecovery;

    // 內部方法
    bool detectProtocolVersion();
    bool detectFeatures();
    bool sendCommandInternal(char cmd0, char cmd1, const uint8_t* payload = nullptr, size_t len = 0);
    bool waitForAck(unsigned long timeout = 200);
    
    // 錯誤處理內部方法
    void setError(S21ErrorCode error);
    void incrementSuccess();
    void incrementError();
    bool validateResponse(const uint8_t* buffer, size_t len);
    bool validatePayload(const uint8_t* payload, size_t len);
    
    // FX 擴展命令內部方法
    bool sendFXCommand(FXCommandType cmdType, uint8_t subCmd, const uint8_t* payload = nullptr, size_t len = 0);
    bool parseFXResponse(FXCommandType expectedType, FXResponse& response);
    bool probeFXCommandSupport(FXCommandType cmdType);
    void initializeFXCommandCapabilities();
    
    // 動態命令發現
    bool discoverAvailableCommands();
    bool testCommandSupport(char cmd0, char cmd1, const uint8_t* testPayload = nullptr, size_t testLen = 0);
    
    // 數據驗證和異常值過濾
    bool validateSensorData(float value, uint8_t sensorType) const;
    float filterOutliers(float newValue, float lastValue, float maxChange, unsigned long timeInterval) const;
    bool isDataStable(float value, float threshold, int requiredSamples) const;
    
    // 協議變體支援
    bool detectProtocolVariant();
    bool switchToVariant(S21ProtocolVariant variant);
    S21ProtocolVariantInfo getCurrentVariantInfo() const;
    
    // 智能錯誤恢復和通訊品質監控 (Phase 4)
    bool attemptErrorRecovery();
    bool performHealthCheck();
    void updateCommunicationQuality();
    bool shouldRetryCommand(S21ErrorCode errorCode, int retryCount) const;
    void resetConnection();
    bool adaptiveCommandTiming();
    void monitorResponseTimes();

public:
    explicit S21Protocol(HardwareSerial& s);
    
    // IS21Protocol interface implementation
    bool begin() override;
    bool sendCommand(char cmd0, char cmd1, const uint8_t* payload = nullptr, size_t len = 0) override;
    bool parseResponse(uint8_t& cmd0, uint8_t& cmd1, uint8_t* payload, size_t& payloadLen) override;
    S21ProtocolVersion getProtocolVersion() const override { return protocolVersion; }
    const S21Features& getFeatures() const override { return features; }
    bool isFeatureSupported(const S21Features& feature) const override;
    bool isCommandSupported(char cmd0, char cmd1) const override;
    
    // 錯誤處理和狀態檢測實現
    const S21Status& getStatus() const override { return status; }
    S21ErrorCode getLastError() const override { return status.lastError; }
    void clearErrors() override;
    bool hasErrors() const override { return status.hasErrors; }
    uint32_t getErrorCount() const override { return status.communicationErrors; }
    float getSuccessRate() const override;
}; 