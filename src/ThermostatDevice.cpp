#include "device/ThermostatDevice.h"
#include "common/Debug.h"
#include "common/RemoteDebugger.h"

// 靜態變量用於記錄上一次輸出的值
static float lastOutputCurrentTemp = 0;
static float lastOutputTargetTemp = 0;
static int lastOutputMode = -1;

// 添加 HomeKit 模式文字轉換函數
const char* getHomeKitModeText(int mode) {
    switch (mode) {
        case HAP_MODE_OFF: return "OFF";
        case HAP_MODE_HEAT: return "HEAT";
        case HAP_MODE_COOL: return "COOL";
        case HAP_MODE_AUTO: return "AUTO";
        default: return "UNKNOWN";
    }
}

ThermostatDevice::ThermostatDevice(IThermostatControl& thermostatControl) 
    : Service::Thermostat(),
      controller(thermostatControl),
      lastUpdateTime(0),
      lastHeartbeatTime(0),
      lastSignificantChange(0) {
    
    // 初始化特性（使用HomeSpan推薦方式，支持NVS存儲）
    currentTemp = new Characteristic::CurrentTemperature(21.0);
    currentTemp->setRange(0, 100);
    
    targetTemp = new Characteristic::TargetTemperature(21.0);
    targetTemp->setRange(MIN_TEMP, MAX_TEMP, TEMP_STEP);
    
    // 初始化模式（CurrentHeatingCoolingState範圍是0-2，沒有AUTO）
    currentMode = new Characteristic::CurrentHeatingCoolingState(HAP_STATE_OFF);
    currentMode->setRange(0, 2);  // OFF, HEAT, COOL
    
    // TargetHeatingCoolingState支持0-3（包含AUTO）
    targetMode = new Characteristic::TargetHeatingCoolingState(HAP_MODE_OFF);
    targetMode->setValidValues(4, 0, 1, 2, 3);  // 數量, OFF, HEAT, COOL, AUTO
    
    // 必需的溫度單位特性（對HomeKit正常運作至關重要）
    displayUnits = new Characteristic::TemperatureDisplayUnits(0);  // 0 = 攝氏度
    
    // 確保特性能觸發 update() 回調
    DEBUG_INFO_PRINT("[Device] 恆溫器特性已配置，等待 HomeKit 連接\n");
    
    DEBUG_INFO_PRINT("[Device] 初始化完成 - 當前溫度：%.1f°C，目標溫度：%.1f°C，當前模式：%d(%s)\n",
                   currentTemp->getVal<float>(), targetTemp->getVal<float>(), 
                   currentMode->getVal(), getHomeKitModeText(currentMode->getVal()));
}

// 用戶在 HomeKit 上設定溫度或模式時，會調用此函數（HomeKit → 設備）
boolean ThermostatDevice::update() {
    DEBUG_INFO_PRINT("[Device] *** HomeKit update() 回調被觸發 ***\n");
    
    bool changed = false;
    
    // 檢查目標模式變更（使用updated()方法檢測變更）
    if (targetMode->updated()) {
        DEBUG_INFO_PRINT("[Device] 收到 HomeKit 模式變更請求：%d(%s) -> %d(%s)\n",
                       targetMode->getVal(), getHomeKitModeText(targetMode->getVal()),
                       targetMode->getNewVal(), getHomeKitModeText(targetMode->getNewVal()));
        
        uint8_t newMode = targetMode->getNewVal<uint8_t>();
        
        // 處理電源和模式的設定順序很重要：
        // 1. 如果要切換到關閉模式，直接關閉電源
        // 2. 如果要切換到運行模式，先設定模式（會自動處理電源）
        if (newMode == HAP_MODE_OFF) {
            DEBUG_INFO_PRINT("[Device] 切換到關閉模式，自動關閉電源\n");
            if (controller.setPower(false)) {
                DEBUG_INFO_PRINT("[Device] 電源自動關閉成功\n");
            }
        }
        // 注意：從關閉模式切換到運行模式時，不在這裡開啟電源
        // 而是在後面的 setTargetMode 中處理，這樣可以確保模式正確
        
        // 只在沒有收到溫度設定時才自動調整溫度
        if (!targetTemp->updated()) {
            float newTargetTemp = targetTemp->getVal<float>();  // 使用當前的目標溫度作為基礎
            float currentTemp = controller.getCurrentTemperature();
            
            // 當切換到製冷模式時，自動調整目標溫度
            if (newMode == HAP_MODE_COOL) {
                // 設置為當前溫度減1度，但不低於最低溫度
                newTargetTemp = currentTemp - 1.0f;
                if (newTargetTemp < MIN_TEMP) newTargetTemp = MIN_TEMP;
                if (newTargetTemp > MAX_TEMP) newTargetTemp = MAX_TEMP;
                
                DEBUG_INFO_PRINT("[Device] 切換到製冷模式，自動調整目標溫度為 %.1f°C\n", newTargetTemp);
            }
            // 當切換到製熱模式時，自動調整目標溫度
            else if (newMode == HAP_MODE_HEAT) {
                // 設置為當前溫度加1度，但不超過最高溫度
                newTargetTemp = currentTemp + 1.0f;
                if (newTargetTemp < MIN_TEMP) newTargetTemp = MIN_TEMP;
                if (newTargetTemp > MAX_TEMP) newTargetTemp = MAX_TEMP;
                
                DEBUG_INFO_PRINT("[Device] 切換到製熱模式，自動調整目標溫度為 %.1f°C\n", newTargetTemp);
            }
            
            // 應用自動調整的溫度
            if (controller.setTargetTemperature(newTargetTemp)) {
                targetTemp->setVal(newTargetTemp);
            }
        }
        
        if (controller.setTargetMode(newMode)) {
            changed = true;
            DEBUG_INFO_PRINT("[Device] 模式變更成功應用\n");
            // 記錄HomeKit操作到遠端調試器
            REMOTE_LOG_HOMEKIT_OP("設定模式", "恆溫器", 
                                 String(targetMode->getVal()) + "(" + getHomeKitModeText(targetMode->getVal()) + ")",
                                 String(newMode) + "(" + getHomeKitModeText(newMode) + ")",
                                 true, "");
        } else {
            DEBUG_WARN_PRINT("[Device] 模式變更請求被拒絕，但繼續處理其他更新\n");
            // 記錄失敗的HomeKit操作
            REMOTE_LOG_HOMEKIT_OP("設定模式", "恆溫器", 
                                 String(targetMode->getVal()) + "(" + getHomeKitModeText(targetMode->getVal()) + ")",
                                 String(newMode) + "(" + getHomeKitModeText(newMode) + ")",
                                 false, "控制器拒絕模式變更");
            // 不直接返回 false，允許繼續處理其他更新
        }
    }
    
    // 檢查目標溫度變更
    if (targetTemp->updated()) {
        DEBUG_INFO_PRINT("[Device] 收到 HomeKit 溫度變更請求：%.1f°C -> %.1f°C\n",
                       targetTemp->getVal<float>(), targetTemp->getNewVal<float>());
        
        float newTemp = targetTemp->getNewVal<float>();
        if (controller.setTargetTemperature(newTemp)) {
            changed = true;
            DEBUG_INFO_PRINT("[Device] 溫度變更成功應用\n");
            // 記錄HomeKit操作到遠端調試器
            REMOTE_LOG_HOMEKIT_OP("設定溫度", "恆溫器", 
                                 String(targetTemp->getVal<float>(), 1) + "°C",
                                 String(newTemp, 1) + "°C",
                                 true, "");
        } else {
            DEBUG_WARN_PRINT("[Device] 溫度變更請求被拒絕，但繼續處理其他更新\n");
            // 記錄失敗的HomeKit操作
            REMOTE_LOG_HOMEKIT_OP("設定溫度", "恆溫器", 
                                 String(targetTemp->getVal<float>(), 1) + "°C",
                                 String(newTemp, 1) + "°C",
                                 false, "控制器拒絕溫度變更");
            // 不直接返回 false，允許繼續處理其他更新
        }
    }
    
    if (changed) {
        DEBUG_INFO_PRINT("[Device] HomeKit 變更處理完成，已應用到設備\n");
        // 立即觸發狀態同步，提供快速響應
        lastUpdateTime = 0; // 重置更新時間，強制下次loop()立即執行同步
    } else {
        DEBUG_INFO_PRINT("[Device] HomeKit update() 被調用但未檢測到變更\n");
    }
    
    return true; // 總是返回 true，讓 HomeKit 認為操作成功
}

// 每秒調用一次，用於同步 HomeKit 和機器狀態（設備 → HomeKit）
void ThermostatDevice::loop() {
    unsigned long currentTime = millis();
    bool changed = false; // 追蹤是否有狀態變更
    
    // 移除調試日誌以節省資源
    
    // 檢查是否需要輸出心跳信息和當前狀態
    if (currentTime - lastHeartbeatTime >= HEARTBEAT_INTERVAL) {
        DEBUG_INFO_PRINT("[Device] HomeSpan loop 運行中... 電源:%s 模式:%d 當前溫度:%.1f°C 目標溫度:%.1f°C\n", 
                        controller.getPower() ? "開啟" : "關閉",
                        controller.getTargetMode(),
                        controller.getCurrentTemperature(),
                        controller.getTargetTemperature());
        lastHeartbeatTime = currentTime;
    }
    
    // 強制每次都執行狀態更新檢查 - 確保 HomeKit 狀態同步
    // 移除間隔限制，讓 HomeKit 狀態同步更頻繁
    lastUpdateTime = currentTime;
    
    controller.update();
    
    // 在Thermostat服務中，電源狀態通過TargetHeatingCoolingState反映
    // 不需要單獨的電源狀態同步
    
    // 同步目標模式 - 電源關閉時強制設為關閉模式
    uint8_t newTargetMode;
    bool devicePowerState = controller.getPower();
    
    if (!devicePowerState) {
        // 電源關閉時，目標模式必須是關閉
        newTargetMode = HAP_MODE_OFF;
    } else {
        // 電源開啟時，使用控制器的目標模式
        newTargetMode = controller.getTargetMode();
        // 確保模式值在有效範圍內
        if (newTargetMode > HAP_MODE_AUTO) {
            newTargetMode = HAP_MODE_OFF;
        }
    }
    
    if (targetMode->getVal() != newTargetMode) {
        targetMode->setVal(newTargetMode);
        targetMode->timeVal(); // 強制更新時間戳，觸發HomeKit通知
        DEBUG_INFO_PRINT("[Device] 更新目標模式：%d(%s) (強制通知) [電源:%s]\n", 
                      newTargetMode, getHomeKitModeText(newTargetMode),
                      devicePowerState ? "開啟" : "關閉");
        changed = true; // 標記有變更，確保HomeKit客戶端收到通知
        lastSignificantChange = currentTime; // 記錄重要變化時間
    }
    
    // 同步目標溫度
    float newTargetTemp = controller.getTargetTemperature();
    if (abs(targetTemp->getVal<float>() - newTargetTemp) >= TEMP_THRESHOLD) {
        targetTemp->setVal(newTargetTemp);
        targetTemp->timeVal(); // 強制更新時間戳，觸發HomeKit通知
        DEBUG_INFO_PRINT("[Device] 更新目標溫度：%.1f°C (強制通知)\n", newTargetTemp);
        changed = true; // 標記有變更，確保HomeKit客戶端收到通知
        lastSignificantChange = currentTime; // 記錄重要變化時間
    }
    
    // 同步當前溫度並強制通知HomeKit客戶端
    float newCurrentTemp = controller.getCurrentTemperature();
    if (abs(currentTemp->getVal<float>() - newCurrentTemp) >= TEMP_THRESHOLD) {
        DEBUG_VERBOSE_PRINT("[Device] 原本溫度：%.1f°C, 新溫度：%.1f°C\n", currentTemp->getVal<float>(), newCurrentTemp);
        currentTemp->setVal(newCurrentTemp);
        currentTemp->timeVal(); // 強制更新時間戳，觸發HomeKit通知
        DEBUG_INFO_PRINT("[Device] 更新當前溫度：%.1f°C (強制通知)\n", newCurrentTemp);
        changed = true; // 標記有變更，確保HomeKit客戶端收到通知
        lastSignificantChange = currentTime; // 記錄重要變化時間
    }
    
    // 更新當前模式 - 必須與電源狀態保持一致
    uint8_t newCurrentMode;
    bool devicePower = controller.getPower();
    
    if (!devicePower) {
        // 設備關閉時，當前模式必須是OFF
        newCurrentMode = HAP_STATE_OFF;
    } else {
        // 設備開啟時，根據目標模式決定當前模式
        uint8_t targetModeValue = controller.getTargetMode();
        switch (targetModeValue) {
            case 1: // 制熱
                newCurrentMode = HAP_STATE_HEAT;
                break;
            case 2: // 制冷
                newCurrentMode = HAP_STATE_COOL;
                break;
            case 3: // 自動 - 根據溫度差決定
                {
                    float tempDiff = controller.getTargetTemperature() - controller.getCurrentTemperature();
                    if (tempDiff > 0.5f) {
                        newCurrentMode = HAP_STATE_HEAT;
                    } else if (tempDiff < -0.5f) {
                        newCurrentMode = HAP_STATE_COOL;
                    } else {
                        newCurrentMode = HAP_STATE_OFF;
                    }
                }
                break;
            default: // 關閉或其他
                newCurrentMode = HAP_STATE_OFF;
                break;
        }
    }
    
    // 只在實際狀態發生變化時更新並強制通知
    if (newCurrentMode != currentMode->getVal()) {
        currentMode->setVal(newCurrentMode);
        currentMode->timeVal(); // 強制更新時間戳，觸發HomeKit通知
        DEBUG_INFO_PRINT("[Device] 更新當前模式：%d(%s) (強制通知) [電源:%s]\n", 
                      newCurrentMode, getHomeKitModeText(newCurrentMode), 
                      devicePower ? "開啟" : "關閉");
        changed = true; // 標記有變更，確保HomeKit客戶端收到通知
        lastSignificantChange = currentTime; // 記錄重要變化時間
    }
    
    // 如果有任何狀態變更，立即通知HomeSpan處理
    if (changed) {
        DEBUG_INFO_PRINT("[Device] 檢測到狀態變更，觸發HomeKit通知\n");
    }
}