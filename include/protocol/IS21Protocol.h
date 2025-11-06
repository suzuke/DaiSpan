#pragma once

#include <Arduino.h>

// S21 協議版本定義 (基於 Faikin 規範)
enum class S21ProtocolVersion {
    UNKNOWN = 0,
    V0 = 0,      // 最早期版本
    V1 = 1,      // 基本功能版本
    V2 = 2,      // 增強功能版本
    V3_00 = 300, // V3.00
    V3_01 = 301, // V3.01
    V3_02 = 302, // V3.02
    V3_10 = 310, // V3.10
    V3_11 = 311, // V3.11
    V3_12 = 312, // V3.12
    V3_20 = 320, // V3.20
    V3_21 = 321, // V3.21
    V3_22 = 322, // V3.22
    V3_30 = 330, // V3.30
    V3_40 = 340, // V3.40
    V3_50 = 350, // V3.50
    V4_00 = 400  // 未來版本預留
};

// 功能支援標誌 (基於 Faikin 規範擴展)
struct S21Features {
    // 基本模式功能
    bool hasAutoMode : 1;           // 支援自動模式
    bool hasDehumidify : 1;         // 支援除濕模式
    bool hasFanMode : 1;            // 支援送風模式
    bool hasHeatMode : 1;           // 支援制熱模式
    bool hasCoolMode : 1;           // 支援制冷模式
    
    // 增強功能
    bool hasPowerfulMode : 1;       // 支援強力模式
    bool hasEcoMode : 1;            // 支援節能模式
    bool hasQuietMode : 1;          // 支援靜音模式
    bool hasComfortMode : 1;        // 支援舒適模式
    
    // 感測器和顯示功能
    bool hasTemperatureDisplay : 1; // 支援溫度顯示
    bool hasHumiditySensor : 1;     // 支援濕度感測器
    bool hasOutdoorTempSensor : 1;  // 支援室外溫度感測器
    bool hasErrorReporting : 1;     // 支援錯誤報告
    
    // 進階控制功能
    bool hasScheduleMode : 1;       // 支援定時功能
    bool hasSwingControl : 1;       // 支援擺風控制
    bool hasVerticalSwing : 1;      // 支援垂直擺風
    bool hasHorizontalSwing : 1;    // 支援水平擺風
    bool hasSwingAngleControl : 1;  // 支援鎖定角度
    bool hasMultiZone : 1;          // 支援多區域控制
    bool hasWiFiModule : 1;         // 支援 WiFi 模組
    
    // V3+ 擴展功能
    bool hasAdvancedFilters : 1;    // 支援進階過濾器
    bool hasEnergyMonitoring : 1;   // 支援能耗監控
    bool hasMaintenanceAlerts : 1;  // 支援維護提醒
    bool hasRemoteDiagnostics : 1;  // 支援遠程診斷
    
    S21Features() : 
        hasAutoMode(false), hasDehumidify(false), hasFanMode(false),
        hasHeatMode(false), hasCoolMode(false), hasPowerfulMode(false),
        hasEcoMode(false), hasQuietMode(false), hasComfortMode(false),
        hasTemperatureDisplay(false), hasHumiditySensor(false),
        hasOutdoorTempSensor(false), hasErrorReporting(false),
        hasScheduleMode(false), hasSwingControl(false),
        hasVerticalSwing(false), hasHorizontalSwing(false),
        hasSwingAngleControl(false), hasMultiZone(false),
        hasWiFiModule(false), hasAdvancedFilters(false), hasEnergyMonitoring(false),
        hasMaintenanceAlerts(false), hasRemoteDiagnostics(false) {}
};

// 錯誤代碼定義 (基於 Faikin 規範)
enum class S21ErrorCode {
    SUCCESS = 0,
    TIMEOUT = 1,
    INVALID_RESPONSE = 2,
    CHECKSUM_ERROR = 3,
    COMMAND_NOT_SUPPORTED = 4,
    PROTOCOL_ERROR = 5,
    BUFFER_OVERFLOW = 6,
    INVALID_PARAMETER = 7,
    COMMUNICATION_ERROR = 8,
    DEVICE_ERROR = 9,
    UNKNOWN_ERROR = 255
};

// 狀態檢測結構
struct S21Status {
    bool isConnected;           // 設備是否連接
    bool hasErrors;             // 是否有錯誤
    S21ErrorCode lastError;     // 最後錯誤代碼
    uint8_t errorFlags;         // 錯誤標誌位
    uint32_t communicationErrors; // 通訊錯誤計數
    uint32_t successfulCommands; // 成功命令計數
    unsigned long lastResponseTime; // 最後回應時間
    
    S21Status() : isConnected(false), hasErrors(false), 
                  lastError(S21ErrorCode::SUCCESS), errorFlags(0),
                  communicationErrors(0), successfulCommands(0),
                  lastResponseTime(0) {}
};

class IS21Protocol {
public:
    virtual ~IS21Protocol() = default;
    
    // 初始化並探測協議版本和功能
    virtual bool begin() = 0;
    
    // 發送命令並等待確認
    virtual bool sendCommand(char cmd0, char cmd1, const uint8_t* payload = nullptr, size_t len = 0) = 0;
    
    // 解析回應
    virtual bool parseResponse(uint8_t& cmd0, uint8_t& cmd1, uint8_t* payload, size_t& payloadLen) = 0;
    
    // 獲取協議版本和功能支援信息
    virtual S21ProtocolVersion getProtocolVersion() const = 0;
    virtual const S21Features& getFeatures() const = 0;
    virtual bool isFeatureSupported(const S21Features& feature) const = 0;
    
    // 檢查命令是否被當前協議版本支援
    virtual bool isCommandSupported(char cmd0, char cmd1) const = 0;
    
    // 錯誤處理和狀態檢測 (新增)
    virtual const S21Status& getStatus() const = 0;
    virtual S21ErrorCode getLastError() const = 0;
    virtual void clearErrors() = 0;
    virtual bool hasErrors() const = 0;
    virtual uint32_t getErrorCount() const = 0;
    virtual float getSuccessRate() const = 0;
}; 
