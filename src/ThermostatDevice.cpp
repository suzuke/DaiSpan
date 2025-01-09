#include "device/ThermostatDevice.h"
#include "common/Debug.h"

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
      lastHeartbeatTime(0) {
    
    // 初始化特性
    active = new Characteristic::Active(0);
    currentTemp = new Characteristic::CurrentTemperature(21.0);
    currentTemp->setRange(0, 100);
    
    targetTemp = new Characteristic::TargetTemperature(21.0);
    targetTemp->setRange(MIN_TEMP, MAX_TEMP, 0.5);
    
    // 初始化模式
    currentMode = new Characteristic::CurrentHeatingCoolingState(0);
    currentMode->setRange(0, 2);  // 確保範圍在 0-2
    
    targetMode = new Characteristic::TargetHeatingCoolingState(0);
    targetMode->setValidValues(0,1,2,3);
    
    // 添加溫度單位特性（攝氏度）
    new Characteristic::TemperatureDisplayUnits(0);  // 0 = 攝氏度
    
    DEBUG_INFO_PRINT("[Device] 初始化完成 - 當前溫度：%.1f°C，目標溫度：%.1f°C，當前模式：%d(%s)\n",
                   currentTemp->getVal(), targetTemp->getVal(), 
                   currentMode->getVal(), getHomeKitModeText(currentMode->getVal()));
}

// 用戶在 HomeKit 上設定溫度或模式時，會調用此函數（HomeKit → 設備）
boolean ThermostatDevice::update() {
    bool changed = false;
    bool hasTemperatureChange = targetTemp->getNewVal() != targetTemp->getVal();
    
    // 處理模式變更
    if (targetMode->getNewVal() != targetMode->getVal()) {
        DEBUG_INFO_PRINT("[Device] 收到 HomeKit 模式變更請求：%d(%s) -> %d(%s)\n",
                       targetMode->getVal(), getHomeKitModeText(targetMode->getVal()),
                       targetMode->getNewVal(), getHomeKitModeText(targetMode->getNewVal()));
        
        uint8_t newMode = targetMode->getNewVal();
        
        // 只在沒有收到溫度設定時才自動調整溫度
        if (!hasTemperatureChange) {
            float newTargetTemp = targetTemp->getVal();  // 使用當前的目標溫度作為基礎
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
        } else {
            DEBUG_INFO_PRINT("[Device] 模式變更請求被拒絕\n");
            return false;
        }
    }
    
    // 處理溫度變更
    if (hasTemperatureChange) {
        DEBUG_INFO_PRINT("[Device] 收到 HomeKit 溫度變更請求：%.1f°C -> %.1f°C\n",
                       targetTemp->getVal(), targetTemp->getNewVal());
        
        float newTemp = targetTemp->getNewVal();
        if (controller.setTargetTemperature(newTemp)) {
            changed = true;
            DEBUG_INFO_PRINT("[Device] 溫度變更成功應用\n");
        } else {
            DEBUG_INFO_PRINT("[Device] 溫度變更請求被拒絕\n");
            return false;
        }
    }
    
    return changed;
}

// 每秒調用一次，用於同步 HomeKit 和機器狀態（設備 → HomeKit）
void ThermostatDevice::loop() {
    unsigned long currentTime = millis();
    
    // 檢查是否需要輸出心跳信息
    if (currentTime - lastHeartbeatTime >= HEARTBEAT_INTERVAL) {
        DEBUG_INFO_PRINT("[Device] HomeSpan loop 運行中...\n");
        lastHeartbeatTime = currentTime;
    }
    
    // 檢查是否需要更新狀態
    if (currentTime - lastUpdateTime < UPDATE_INTERVAL) {
        return;
    }
    lastUpdateTime = currentTime;
    
    controller.update();
    
    // 同步電源狀態
    bool newPowerState = controller.getPower();
    if (active->getVal() != newPowerState) {
        active->setVal(newPowerState);
        DEBUG_INFO_PRINT("[Device] 更新電源狀態：%s\n", newPowerState ? "開啟" : "關閉");
    }
    
    // 同步目標模式
    uint8_t newTargetMode = controller.getTargetMode();
    if (targetMode->getVal() != newTargetMode) {
        targetMode->setVal(newTargetMode);
        DEBUG_INFO_PRINT("[Device] 更新目標模式：%d(%s)\n", 
                      newTargetMode, getHomeKitModeText(newTargetMode));
    }
    
    // 同步目標溫度
    float newTargetTemp = controller.getTargetTemperature();
    if (abs(targetTemp->getVal<float>() - newTargetTemp) >= TEMP_THRESHOLD) {
        targetTemp->setVal(newTargetTemp);
        DEBUG_INFO_PRINT("[Device] 更新目標溫度：%.1f°C\n", newTargetTemp);
    }
    
    // 同步當前溫度
    float newCurrentTemp = controller.getCurrentTemperature();
    if (abs(currentTemp->getVal<float>() - newCurrentTemp) >= TEMP_THRESHOLD) {
        DEBUG_VERBOSE_PRINT("[Device] 原本溫度：%.1f°C, 新溫度：%.1f°C\n", currentTemp->getVal<float>(), newCurrentTemp);
        currentTemp->setVal(newCurrentTemp);
        DEBUG_INFO_PRINT("[Device] 更新當前溫度：%.1f°C\n", newCurrentTemp);
    }
    
    // 更新當前模式
    uint8_t newCurrentMode;
    if (!controller.getPower()) {
        newCurrentMode = HAP_MODE_OFF;
    } else {
        uint8_t mode = controller.getTargetMode();
        if (mode == HAP_MODE_AUTO) {
            // 在 AUTO 模式下，根據當前溫度和目標溫度決定實際狀態
            float targetTemp = controller.getTargetTemperature();
            float currentTemp = controller.getCurrentTemperature();
            if (currentTemp < targetTemp - 0.5) {  // 添加溫差閾值
                newCurrentMode = HAP_MODE_HEAT;
            } else if (currentTemp > targetTemp + 0.5) {  // 添加溫差閾值
                newCurrentMode = HAP_MODE_COOL;
            } else {
                newCurrentMode = HAP_MODE_OFF;
            }
        } else {
            newCurrentMode = mode;
        }
    }
    
    // 只在實際狀態發生變化時更新
    if (newCurrentMode != currentMode->getVal()) {
        currentMode->setVal(newCurrentMode);
        DEBUG_INFO_PRINT("[Device] 更新當前模式：%d(%s)\n", 
                      newCurrentMode, getHomeKitModeText(newCurrentMode));
    }
}