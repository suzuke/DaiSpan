#pragma once

#include "HomeSpan.h"
#include <deque>
#include "../controller/IThermostatControl.h"
#include "../common/Debug.h"

// HomeKit 恆溫器模式定義
#define HAP_MODE_OFF        0
#define HAP_MODE_HEAT       1
#define HAP_MODE_COOL       2
#define HAP_MODE_AUTO       3

// HomeKit 恆溫器狀態定義
#define HAP_STATE_OFF       0
#define HAP_STATE_HEAT      1
#define HAP_STATE_COOL      2

// 溫度範圍和閾值
#define TEMP_THRESHOLD    0.2f   // 溫度變化閾值（與 Controller 保持一致）
#define MIN_TEMP         16.0f   // 最低溫度限制
#define MAX_TEMP         30.0f   // 最高溫度限制
#define TEMP_STEP        0.5f    // 溫度調節步長

// 更新間隔設定（毫秒）- 優化HomeKit響應速度  
#define UPDATE_INTERVAL    1000   // 狀態更新間隔（1秒，確保 HomeKit 狀態同步）
#define TEMP_UPDATE_PRIORITY_INTERVAL 1000  // 溫度變化時的優先更新間隔（1秒）
// HEARTBEAT_INTERVAL 已在 Debug.h 中定義

class ThermostatDevice : public Service::Thermostat {
private:
    IThermostatControl& controller;
    // 使用直接實例而非指針（HomeSpan推薦方式）
    SpanCharacteristic* currentTemp;
    SpanCharacteristic* targetTemp;
    SpanCharacteristic* currentMode;
    SpanCharacteristic* targetMode;
    SpanCharacteristic* displayUnits;  // 必需的溫度單位特性
    unsigned long lastUpdateTime;     // 最後狀態更新時間
    unsigned long lastHeartbeatTime;  // 最後心跳時間
    unsigned long lastSignificantChange; // 最後重要狀態變化時間
    
    enum class CommandType {
        Mode,
        Temperature
    };

    struct PendingCommand {
        CommandType type;
        uint8_t modeValue;
        uint8_t prevMode;
        float tempValue;
        float prevTemp;
    };

    std::deque<PendingCommand> commandQueue;
    bool pendingHomeKitSync = false;
    static constexpr unsigned long COMMAND_PROCESS_INTERVAL = 50; // 每 50ms 檢查一次佇列
    unsigned long lastCommandProcess = 0;
    
    // 添加模式文字轉換函數
    static const char* getHomeKitModeText(int mode) {
        switch (mode) {
            case HAP_MODE_OFF: return "OFF";
            case HAP_MODE_HEAT: return "HEAT";
            case HAP_MODE_COOL: return "COOL";
            case HAP_MODE_AUTO: return "AUTO";
            default: return "UNKNOWN";
        }
    }
    
    // 輔助方法
    void autoAdjustTemperatureForMode(uint8_t mode);
    void handleSuccessfulUpdate();
    
    // 同步方法
    bool syncTargetMode(unsigned long currentTime);
    bool syncTargetTemperature(unsigned long currentTime);
    bool syncCurrentTemperature(unsigned long currentTime);
    bool syncCurrentMode(unsigned long currentTime);
    uint8_t calculateAutoModeState();
    
    void enqueueModeCommand(uint8_t hapMode);
    void enqueueTemperatureCommand(float temperature);
    enum class CommandExecutionStatus {
        NoCommand,
        Success,
        Failure
    };
    CommandExecutionStatus processCommandQueue(unsigned long currentTime);
    bool executeCommand(const PendingCommand& command);
    void applyCommandSuccess(const PendingCommand& command);
    void rollbackCommand(const PendingCommand& command);
    void flagHomeKitSyncNeeded();
    
public:
    explicit ThermostatDevice(IThermostatControl& ctrl);
    void loop() override;
    boolean update() override;
}; 
