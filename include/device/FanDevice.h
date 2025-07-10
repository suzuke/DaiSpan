#pragma once

#include "HomeSpan.h"
#include "../controller/IThermostatControl.h"
#include "../common/Debug.h"
#include "../common/ThermostatMode.h"

// HomeKit 風扇速度映射定義 (針對真實AC優化)
#define HOMEKIT_FAN_OFF     0     // 關閉
#define HOMEKIT_FAN_AUTO    20    // 自動 (20%)
#define HOMEKIT_FAN_QUIET   10    // 安靜 (10%) - 如果AC支持
#define HOMEKIT_FAN_SPEED_1 10    // 1檔 (10%) - 真實AC最低速
#define HOMEKIT_FAN_SPEED_2 35    // 2檔 (35%)
#define HOMEKIT_FAN_SPEED_3 55    // 3檔 (55%)
#define HOMEKIT_FAN_SPEED_4 80    // 4檔 (80%)
#define HOMEKIT_FAN_SPEED_5 100   // 5檔 (100%)

// 更新間隔設定（優化HomeKit響應速度）
#define FAN_UPDATE_INTERVAL  2000   // 風扇狀態更新間隔（2秒，快速響應HomeKit操作）

class FanDevice : public Service::Fan {
private:
    IThermostatControl& controller;
    SpanCharacteristic* fanOn;
    SpanCharacteristic* fanSpeed;
    unsigned long lastUpdateTime;
    unsigned long lastUserInteraction;  // 最後用戶操作時間
    int lastUserSetSpeed;              // 最後用戶設置的速度
    
    // 內部輔助方法
    uint8_t homeKitSpeedToACSpeed(int homeKitSpeed);
    int acSpeedToHomeKitSpeed(uint8_t acSpeed);
    bool isFanEffectivelyOn(uint8_t acSpeed);
    
public:
    explicit FanDevice(IThermostatControl& ctrl);
    void loop() override;
    boolean update() override;
};