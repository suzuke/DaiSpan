#include "device/FanDevice.h"
#include "common/Debug.h"
#include "common/RemoteDebugger.h"

FanDevice::FanDevice(IThermostatControl& ctrl) 
    : Service::Fan(),
      controller(ctrl),
      lastUpdateTime(0),
      lastUserInteraction(0),
      lastUserSetSpeed(-1) {
    
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
        
        // 立即記錄用戶互動
        lastUserInteraction = millis();
        
        if (newOnState) {
            // 開啟風扇 - 如果速度為0，設置為自動
            int currentSpeed = fanSpeed->getVal();
            if (currentSpeed == 0) {
                fanSpeed->setVal(HOMEKIT_FAN_AUTO);
                currentSpeed = HOMEKIT_FAN_AUTO;
                DEBUG_INFO_PRINT("[FanDevice] 風扇開啟，自動設置速度為：%d%%\n", currentSpeed);
            }
            
            lastUserSetSpeed = currentSpeed;
            DEBUG_INFO_PRINT("[FanDevice] 記錄用戶開關互動 - 時間: %lu, 速度: %d%%\n", 
                           lastUserInteraction, lastUserSetSpeed);
            
            uint8_t acSpeed = homeKitSpeedToACSpeed(currentSpeed);
            if (controller.setFanSpeed(acSpeed)) {
                changed = true;
                DEBUG_INFO_PRINT("[FanDevice] 風扇開啟成功，AC速度：%d\n", acSpeed);
                // 記錄HomeKit操作到遠端調試器
                REMOTE_LOG_HOMEKIT_OP("開啟風扇", "風扇", 
                                     "關閉",
                                     "開啟 " + String(currentSpeed) + "%",
                                     true, "");
            } else {
                // 記錄失敗的HomeKit操作
                REMOTE_LOG_HOMEKIT_OP("開啟風扇", "風扇", 
                                     "關閉",
                                     "開啟 " + String(currentSpeed) + "%",
                                     false, "控制器拒絕風扇開啟");
            }
        } else {
            // 關閉風扇 - 設置為自動模式但不強制關閉空調
            if (controller.setFanSpeed(FAN_AUTO)) {
                fanSpeed->setVal(HOMEKIT_FAN_AUTO);
                lastUserSetSpeed = HOMEKIT_FAN_AUTO;
                changed = true;
                DEBUG_INFO_PRINT("[FanDevice] 風扇設置為自動模式\n");
                DEBUG_INFO_PRINT("[FanDevice] 記錄用戶關閉互動 - 時間: %lu, 速度: %d%%\n", 
                               lastUserInteraction, lastUserSetSpeed);
                // 記錄HomeKit操作到遠端調試器
                REMOTE_LOG_HOMEKIT_OP("關閉風扇", "風扇", 
                                     "開啟",
                                     "關閉 (自動模式)",
                                     true, "");
            } else {
                // 記錄失敗的HomeKit操作
                REMOTE_LOG_HOMEKIT_OP("關閉風扇", "風扇", 
                                     "開啟",
                                     "關閉 (自動模式)",
                                     false, "控制器拒絕風扇關閉");
            }
        }
    }
    
    // 檢查風扇速度變更
    if (fanSpeed->updated()) {
        int newSpeed = fanSpeed->getNewVal();
        DEBUG_INFO_PRINT("[FanDevice] 收到風扇速度變更：%d%% -> %d%%\n",
                       fanSpeed->getVal(), newSpeed);
        
        // 立即記錄用戶互動
        lastUserInteraction = millis();
        lastUserSetSpeed = newSpeed;
        DEBUG_INFO_PRINT("[FanDevice] 記錄用戶速度互動 - 時間: %lu, 速度: %d%%\n", 
                       lastUserInteraction, lastUserSetSpeed);
        
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
            // 記錄HomeKit操作到遠端調試器
            REMOTE_LOG_HOMEKIT_OP("設定風扇速度", "風扇", 
                                 String(fanSpeed->getVal()) + "%",
                                 String(newSpeed) + "%",
                                 true, "");
        } else {
            DEBUG_WARN_PRINT("[FanDevice] 風扇速度設置失敗，但繼續處理\n");
            // 記錄失敗的HomeKit操作
            REMOTE_LOG_HOMEKIT_OP("設定風扇速度", "風扇", 
                                 String(fanSpeed->getVal()) + "%",
                                 String(newSpeed) + "%",
                                 false, "控制器拒絕風扇速度變更");
            // 不直接返回 false，允許 HomeKit 操作繼續
        }
    }
    
    if (changed) {
        DEBUG_INFO_PRINT("[FanDevice] HomeKit 風扇變更處理完成\n");
        // 立即觸發狀態同步，提供快速響應
        lastUpdateTime = 0; // 重置更新時間，強制下次loop()立即執行同步
    } else {
        DEBUG_INFO_PRINT("[FanDevice] HomeKit 風扇 update() 被調用但未檢測到變更\n");
    }
    
    return true; // 總是返回 true，讓 HomeKit 認為操作成功
}

// 每秒調用一次，用於同步 HomeKit 和設備狀態
void FanDevice::loop() {
    unsigned long currentTime = millis();
    
    // 檢查是否需要更新狀態
    if (currentTime - lastUpdateTime < FAN_UPDATE_INTERVAL) {
        return;
    }
    lastUpdateTime = currentTime;
    
    // 檢查是否在用戶互動保護期內 (5秒內不覆蓋用戶設置)
    const unsigned long USER_INTERACTION_GRACE_PERIOD = 5000; // 5秒保護期
    bool inGracePeriod = (lastUserInteraction > 0) && 
                        ((currentTime - lastUserInteraction) < USER_INTERACTION_GRACE_PERIOD);
    
    if (inGracePeriod) {
        DEBUG_INFO_PRINT("[FanDevice] 用戶互動保護期內，跳過狀態同步 (剩餘: %lu ms)\n",
                          USER_INTERACTION_GRACE_PERIOD - (currentTime - lastUserInteraction));
        return;
    }
    
    DEBUG_VERBOSE_PRINT("[FanDevice] 執行狀態同步 - 上次用戶互動: %lu ms 前\n",
                       lastUserInteraction > 0 ? (currentTime - lastUserInteraction) : 0);
    
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
    
    // 同步速度狀態 - 檢查是否與用戶上次設置不同
    if (fanSpeed->getVal() != currentHomeKitSpeed) {
        DEBUG_INFO_PRINT("[FanDevice] 發現速度差異 - HomeKit: %d%%, AC對應: %d%% (AC速度: %d)\n",
                        fanSpeed->getVal(), currentHomeKitSpeed, currentACSpeed);
        
        // 如果用戶最近設置過速度，且當前AC速度與用戶設置相符，則不覆蓋
        if (lastUserSetSpeed >= 0 && 
            homeKitSpeedToACSpeed(lastUserSetSpeed) == currentACSpeed) {
            DEBUG_INFO_PRINT("[FanDevice] AC速度 %d 與用戶設置 %d%% 相符，保持用戶設置\n",
                              currentACSpeed, lastUserSetSpeed);
        } else {
            fanSpeed->setVal(currentHomeKitSpeed);
            fanSpeed->timeVal(); // 強制更新時間戳
            DEBUG_INFO_PRINT("[FanDevice] 更新風扇速度：%d%% (AC速度：%d，強制通知)\n",
                           currentHomeKitSpeed, currentACSpeed);
            stateChanged = true;
        }
    } else {
        DEBUG_VERBOSE_PRINT("[FanDevice] 風扇速度無變化：%d%%\n", fanSpeed->getVal());
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
        return FAN_SPEED_1; // 1-15% = 1檔 (真實AC最低速)
    } else if (homeKitSpeed <= 25) {
        return FAN_AUTO;  // 16-25% = 自動
    } else if (homeKitSpeed <= 45) {
        return FAN_SPEED_2; // 26-45% = 2檔
    } else if (homeKitSpeed <= 67) {
        return FAN_SPEED_3; // 46-67% = 3檔
    } else if (homeKitSpeed <= 89) {
        return FAN_SPEED_4; // 68-89% = 4檔
    } else {
        return FAN_SPEED_5; // 90-100% = 5檔
    }
}

// 將AC速度轉換為HomeKit速度
int FanDevice::acSpeedToHomeKitSpeed(uint8_t acSpeed) {
    switch (acSpeed) {
        case FAN_AUTO:    return HOMEKIT_FAN_AUTO;    // 20%
        case FAN_QUIET:   return HOMEKIT_FAN_QUIET;   // 10%
        case FAN_SPEED_1: return HOMEKIT_FAN_SPEED_1; // 10%
        case FAN_SPEED_2: return HOMEKIT_FAN_SPEED_2; // 35%
        case FAN_SPEED_3: return HOMEKIT_FAN_SPEED_3; // 55%
        case FAN_SPEED_4: return HOMEKIT_FAN_SPEED_4; // 80%
        case FAN_SPEED_5: return HOMEKIT_FAN_SPEED_5; // 100%
        default:          return HOMEKIT_FAN_AUTO;    // 預設自動
    }
}

// 判斷風扇是否實際開啟
bool FanDevice::isFanEffectivelyOn(uint8_t acSpeed) {
    // 除了自動模式外，其他模式都視為"開啟"
    return (acSpeed != FAN_AUTO);
}