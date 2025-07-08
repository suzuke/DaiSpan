#include "device/FanDevice.h"
#include "common/Debug.h"

FanDevice::FanDevice(IThermostatControl& ctrl) 
    : Service::Fan(),
      controller(ctrl),
      lastUpdateTime(0) {
    
    // 初始化風扇特性
    fanOn = new Characteristic::On(false);  // 風扇開關
    fanSpeed = new Characteristic::RotationSpeed(0);  // 風扇轉速 (0-100%)
    fanSpeed->setRange(0, 100, 10);  // 0-100%，步長為10%
    
    DEBUG_INFO_PRINT("[FanDevice] 風扇服務初始化完成\n");
    
    // 初始化時同步狀態
    uint8_t currentACSpeed = controller.getFanSpeed();
    int homeKitSpeed = acSpeedToHomeKitSpeed(currentACSpeed);
    bool isOn = isFanEffectivelyOn(currentACSpeed);
    
    fanOn->setVal(isOn);
    fanSpeed->setVal(homeKitSpeed);
    
    DEBUG_INFO_PRINT("[FanDevice] 初始狀態 - 開關：%s，速度：%d%% (AC速度：%d)\n",
                   isOn ? "開啟" : "關閉", homeKitSpeed, currentACSpeed);
}

// 用戶在 HomeKit 上設定風扇時，會調用此函數
boolean FanDevice::update() {
    DEBUG_INFO_PRINT("[FanDevice] *** HomeKit 風扇 update() 回調被觸發 ***\n");
    
    bool changed = false;
    
    // 檢查風扇開關變更
    if (fanOn->updated()) {
        bool newOnState = fanOn->getNewVal();
        DEBUG_INFO_PRINT("[FanDevice] 收到風扇開關變更：%s -> %s\n",
                       fanOn->getVal() ? "開啟" : "關閉",
                       newOnState ? "開啟" : "關閉");
        
        if (newOnState) {
            // 開啟風扇 - 如果速度為0，設置為自動
            int currentSpeed = fanSpeed->getVal();
            if (currentSpeed == 0) {
                fanSpeed->setVal(HOMEKIT_FAN_AUTO);
                currentSpeed = HOMEKIT_FAN_AUTO;
                DEBUG_INFO_PRINT("[FanDevice] 風扇開啟，自動設置速度為：%d%%\n", currentSpeed);
            }
            
            uint8_t acSpeed = homeKitSpeedToACSpeed(currentSpeed);
            if (controller.setFanSpeed(acSpeed)) {
                changed = true;
                DEBUG_INFO_PRINT("[FanDevice] 風扇開啟成功，AC速度：%d\n", acSpeed);
            }
        } else {
            // 關閉風扇 - 設置為自動模式但不強制關閉空調
            if (controller.setFanSpeed(FAN_AUTO)) {
                fanSpeed->setVal(HOMEKIT_FAN_AUTO);
                changed = true;
                DEBUG_INFO_PRINT("[FanDevice] 風扇設置為自動模式\n");
            }
        }
    }
    
    // 檢查風扇速度變更
    if (fanSpeed->updated()) {
        int newSpeed = fanSpeed->getNewVal();
        DEBUG_INFO_PRINT("[FanDevice] 收到風扇速度變更：%d%% -> %d%%\n",
                       fanSpeed->getVal(), newSpeed);
        
        uint8_t acSpeed = homeKitSpeedToACSpeed(newSpeed);
        if (controller.setFanSpeed(acSpeed)) {
            // 根據速度自動調整開關狀態
            bool shouldBeOn = isFanEffectivelyOn(acSpeed);
            if (fanOn->getVal() != shouldBeOn) {
                fanOn->setVal(shouldBeOn);
                DEBUG_INFO_PRINT("[FanDevice] 根據速度自動調整開關：%s\n",
                               shouldBeOn ? "開啟" : "關閉");
            }
            changed = true;
            DEBUG_INFO_PRINT("[FanDevice] 風扇速度設置成功，AC速度：%d\n", acSpeed);
        } else {
            DEBUG_ERROR_PRINT("[FanDevice] 風扇速度設置失敗\n");
            return false;
        }
    }
    
    if (changed) {
        DEBUG_INFO_PRINT("[FanDevice] HomeKit 風扇變更處理完成\n");
    } else {
        DEBUG_INFO_PRINT("[FanDevice] HomeKit 風扇 update() 被調用但未檢測到變更\n");
    }
    
    return changed;
}

// 每秒調用一次，用於同步 HomeKit 和設備狀態
void FanDevice::loop() {
    unsigned long currentTime = millis();
    
    // 檢查是否需要更新狀態
    if (currentTime - lastUpdateTime < FAN_UPDATE_INTERVAL) {
        return;
    }
    lastUpdateTime = currentTime;
    
    // 同步風扇狀態
    uint8_t currentACSpeed = controller.getFanSpeed();
    int currentHomeKitSpeed = acSpeedToHomeKitSpeed(currentACSpeed);
    bool currentIsOn = isFanEffectivelyOn(currentACSpeed);
    
    bool stateChanged = false;
    
    // 同步開關狀態
    if (fanOn->getVal() != currentIsOn) {
        fanOn->setVal(currentIsOn);
        fanOn->timeVal(); // 強制更新時間戳
        DEBUG_INFO_PRINT("[FanDevice] 更新風扇開關：%s (強制通知)\n",
                       currentIsOn ? "開啟" : "關閉");
        stateChanged = true;
    }
    
    // 同步速度狀態
    if (fanSpeed->getVal() != currentHomeKitSpeed) {
        fanSpeed->setVal(currentHomeKitSpeed);
        fanSpeed->timeVal(); // 強制更新時間戳
        DEBUG_INFO_PRINT("[FanDevice] 更新風扇速度：%d%% (AC速度：%d，強制通知)\n",
                       currentHomeKitSpeed, currentACSpeed);
        stateChanged = true;
    }
    
    if (stateChanged) {
        DEBUG_VERBOSE_PRINT("[FanDevice] 風扇狀態已同步到HomeKit\n");
    }
}

// 將HomeKit速度轉換為AC速度
uint8_t FanDevice::homeKitSpeedToACSpeed(int homeKitSpeed) {
    if (homeKitSpeed == 0) {
        return FAN_AUTO;  // 0% = 自動
    } else if (homeKitSpeed <= 15) {
        return FAN_QUIET; // 1-15% = 安靜
    } else if (homeKitSpeed <= 25) {
        return FAN_AUTO;  // 16-25% = 自動
    } else if (homeKitSpeed <= 40) {
        return FAN_SPEED_1; // 26-40% = 1檔
    } else if (homeKitSpeed <= 60) {
        return FAN_SPEED_2; // 41-60% = 2檔
    } else if (homeKitSpeed <= 80) {
        return FAN_SPEED_3; // 61-80% = 3檔
    } else if (homeKitSpeed <= 95) {
        return FAN_SPEED_4; // 81-95% = 4檔
    } else {
        return FAN_SPEED_5; // 96-100% = 5檔
    }
}

// 將AC速度轉換為HomeKit速度
int FanDevice::acSpeedToHomeKitSpeed(uint8_t acSpeed) {
    switch (acSpeed) {
        case FAN_AUTO:    return HOMEKIT_FAN_AUTO;    // 20%
        case FAN_QUIET:   return HOMEKIT_FAN_QUIET;   // 10%
        case FAN_SPEED_1: return HOMEKIT_FAN_SPEED_1; // 30%
        case FAN_SPEED_2: return HOMEKIT_FAN_SPEED_2; // 50%
        case FAN_SPEED_3: return HOMEKIT_FAN_SPEED_3; // 70%
        case FAN_SPEED_4: return HOMEKIT_FAN_SPEED_4; // 90%
        case FAN_SPEED_5: return HOMEKIT_FAN_SPEED_5; // 100%
        default:          return HOMEKIT_FAN_AUTO;    // 預設自動
    }
}

// 判斷風扇是否實際開啟
bool FanDevice::isFanEffectivelyOn(uint8_t acSpeed) {
    // 除了自動模式外，其他模式都視為"開啟"
    return (acSpeed != FAN_AUTO);
}