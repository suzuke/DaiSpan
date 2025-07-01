#pragma once

#include "HomeSpan.h"
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

// 更新間隔設定（毫秒）- 與 ThermostatController 保持一致
#define UPDATE_INTERVAL    8000   // 狀態更新間隔（8秒，減少協議查詢頻率）
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
    
public:
    explicit ThermostatDevice(IThermostatControl& ctrl);
    void loop() override;
    boolean update() override;
}; 