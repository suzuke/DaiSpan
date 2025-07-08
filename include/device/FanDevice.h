#pragma once

#include "HomeSpan.h"
#include "../controller/IThermostatControl.h"
#include "../common/Debug.h"
#include "../common/ThermostatMode.h"

// HomeKit 風扇速度映射定義
#define HOMEKIT_FAN_OFF     0     // 關閉
#define HOMEKIT_FAN_AUTO    20    // 自動 (20%)
#define HOMEKIT_FAN_QUIET   10    // 安靜 (10%)
#define HOMEKIT_FAN_SPEED_1 30    // 1檔 (30%)
#define HOMEKIT_FAN_SPEED_2 50    // 2檔 (50%)
#define HOMEKIT_FAN_SPEED_3 70    // 3檔 (70%)
#define HOMEKIT_FAN_SPEED_4 90    // 4檔 (90%)
#define HOMEKIT_FAN_SPEED_5 100   // 5檔 (100%)

// 更新間隔設定（與恆溫器保持一致）
#define FAN_UPDATE_INTERVAL  8000   // 風扇狀態更新間隔（8秒）

class FanDevice : public Service::Fan {
private:
    IThermostatControl& controller;
    SpanCharacteristic* fanOn;
    SpanCharacteristic* fanSpeed;
    unsigned long lastUpdateTime;
    
    // 內部輔助方法
    uint8_t homeKitSpeedToACSpeed(int homeKitSpeed);
    int acSpeedToHomeKitSpeed(uint8_t acSpeed);
    bool isFanEffectivelyOn(uint8_t acSpeed);
    
public:
    explicit FanDevice(IThermostatControl& ctrl);
    void loop() override;
    boolean update() override;
};