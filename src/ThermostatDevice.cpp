#include "ThermostatDevice.h"
#include "Debug.h"

#define TEMP_THRESHOLD 0.5  // 溫度變化閾值
#define MIN_TEMP 16.0f     // 最低溫度限制
#define MAX_TEMP 30.0f     // 最高溫度限制

// 靜態變量用於記錄上一次輸出的值
static float lastOutputCurrentTemp = 0;
static float lastOutputTargetTemp = 0;
static int lastOutputMode = -1;

// 添加 HomeKit 模式文字轉換函數
const char* getHomeKitModeText(int mode) {
    switch (mode) {
        case 0: return "OFF";
        case 1: return "HEAT";
        case 2: return "COOL";
        case 3: return "AUTO";
        default: return "UNKNOWN";
    }
}

ThermostatDevice::ThermostatDevice(IThermostatControl* thermostatControl) : Service::Thermostat() {
  controller = thermostatControl;
  
  // 初始化上一次輸出的值
  lastOutputCurrentTemp = controller->getCurrentTemperature();
  lastOutputTargetTemp = lastOutputCurrentTemp;
  lastOutputMode = static_cast<int>(controller->getCurrentMode());
  
  // 初始化特性
  currentTemp = new Characteristic::CurrentTemperature(lastOutputCurrentTemp);
  currentTemp->setRange(0, 100);
  
  targetTemp = new Characteristic::TargetTemperature(lastOutputCurrentTemp);
  targetTemp->setRange(MIN_TEMP, MAX_TEMP, 0.5);
  
  // 初始化模式
  ThermostatMode initialMode = controller->getCurrentMode();
  currentMode = new Characteristic::CurrentHeatingCoolingState(static_cast<int>(initialMode));
  currentMode->setRange(0, 2);  // 確保範圍在 0-2
  
  targetMode = new Characteristic::TargetHeatingCoolingState(static_cast<int>(initialMode));
  targetMode->setValidValues(0,1,2,3);
  
  // 添加溫度單位特性（攝氏度）
  new Characteristic::TemperatureDisplayUnits(0);  // 0 = 攝氏度
  
  #ifdef DEBUG_MODE
  DEBUG_INFO_PRINT("[Device] 初始化 - 當前溫度：%.1f°C，目標溫度：%.1f°C，當前模式：%d(%s)\n",
                   lastOutputCurrentTemp, lastOutputCurrentTemp, 
                   static_cast<int>(initialMode), getHomeKitModeText(static_cast<int>(initialMode)));
  #endif
}

// 用戶在 HomeKit 上設定溫度或模式時，會調用此函數（HomeKit → 設備）
boolean ThermostatDevice::update() {
  bool changed = false;
  bool hasTemperatureChange = targetTemp->getNewVal() != targetTemp->getVal();
  
  // 處理模式變更
  if (targetMode->getNewVal() != targetMode->getVal()) {
    #ifdef DEBUG_MODE
    DEBUG_INFO_PRINT("[Device] 收到 HomeKit 模式變更請求：%d(%s) -> %d(%s)\n",
                     targetMode->getVal(), getHomeKitModeText(targetMode->getVal()),
                     targetMode->getNewVal(), getHomeKitModeText(targetMode->getNewVal()));
    #endif
    
    ThermostatMode newMode = static_cast<ThermostatMode>(targetMode->getNewVal());
    
    // 只在沒有收到溫度設定時才自動調整溫度
    if (!hasTemperatureChange) {
      float newTargetTemp = targetTemp->getVal();  // 使用當前的目標溫度作為基礎
      float currentTemp = controller->getCurrentTemperature();
      
      // 當切換到製冷模式時，自動調整目標溫度
      if (newMode == ThermostatMode::COOL) {
        // 設置為當前溫度減1度，但不低於最低溫度
        newTargetTemp = currentTemp - 1.0f;
        if (newTargetTemp < MIN_TEMP) newTargetTemp = MIN_TEMP;
        if (newTargetTemp > MAX_TEMP) newTargetTemp = MAX_TEMP;
        
        #ifdef DEBUG_MODE
        DEBUG_INFO_PRINT("[Device] 切換到製冷模式，自動調整目標溫度為 %.1f°C\n", newTargetTemp);
        #endif
      }
      // 當切換到製熱模式時，自動調整目標溫度
      else if (newMode == ThermostatMode::HEAT) {
        // 設置為當前溫度加1度，但不超過最高溫度
        newTargetTemp = currentTemp + 1.0f;
        if (newTargetTemp < MIN_TEMP) newTargetTemp = MIN_TEMP;
        if (newTargetTemp > MAX_TEMP) newTargetTemp = MAX_TEMP;
        
        #ifdef DEBUG_MODE
        DEBUG_INFO_PRINT("[Device] 切換到製熱模式，自動調整目標溫度為 %.1f°C\n", newTargetTemp);
        #endif
      }
      
      // 應用自動調整的溫度
      if (controller->setTargetTemperature(newTargetTemp)) {
        targetTemp->setVal(newTargetTemp);
      }
    }
    
    if (controller->setTargetMode(newMode)) {
      changed = true;
      #ifdef DEBUG_MODE
      DEBUG_INFO_PRINT("[Device] 模式變更成功應用\n");
      #endif
    } else {
      #ifdef DEBUG_MODE
      DEBUG_INFO_PRINT("[Device] 模式變更請求被拒絕\n");
      #endif
      return false;
    }
  }
  
  // 處理溫度變更
  if (hasTemperatureChange) {
    #ifdef DEBUG_MODE
    DEBUG_INFO_PRINT("[Device] 收到 HomeKit 溫度變更請求：%.1f°C -> %.1f°C\n",
                     targetTemp->getVal(), targetTemp->getNewVal());
    #endif
    
    float newTemp = targetTemp->getNewVal();
    if (controller->setTargetTemperature(newTemp)) {
      changed = true;
      #ifdef DEBUG_MODE
      DEBUG_INFO_PRINT("[Device] 溫度變更成功應用\n");
      #endif
    } else {
      #ifdef DEBUG_MODE
      DEBUG_INFO_PRINT("[Device] 溫度變更請求被拒絕\n");
      #endif
      return false;
    }
  }
  
  return changed;
}

// 每秒調用一次，用於同步 HomeKit 和機器狀態（設備 → HomeKit）
void ThermostatDevice::loop() {
  static unsigned long lastLoopTime = 0;
  unsigned long currentTime = millis();
  
  // 每5秒輸出一次心跳信息
  if (currentTime - lastLoopTime >= 5000) {
    DEBUG_INFO_PRINT("[Device] HomeSpan loop 運行中...\n");
    lastLoopTime = currentTime;
  }
  
  controller->loop();
  
  // 同步目標值
  ThermostatMode newTargetMode = controller->getTargetMode();
  int newTargetModeVal = static_cast<int>(newTargetMode);
  if (targetMode->getVal() != newTargetModeVal) {
    targetMode->setVal(newTargetModeVal);
    #ifdef DEBUG_MODE
    if (newTargetModeVal != lastOutputMode) {
      DEBUG_INFO_PRINT("[Device] 更新目標模式：%d(%s)\n", 
                      newTargetModeVal, getHomeKitModeText(newTargetModeVal));
      lastOutputMode = newTargetModeVal;
    }
    #endif
  }
  
  float newTargetTemp = controller->getTargetTemperature();
  if (abs(targetTemp->getVal() - newTargetTemp) >= TEMP_THRESHOLD) {
    targetTemp->setVal(newTargetTemp);
    #ifdef DEBUG_MODE
    if (abs(newTargetTemp - lastOutputTargetTemp) >= TEMP_THRESHOLD) {
      DEBUG_INFO_PRINT("[Device] 更新目標溫度：%.1f°C\n", newTargetTemp);
      lastOutputTargetTemp = newTargetTemp;
    }
    #endif
  }
  
  // 同步當前值
  float newCurrentTemp = controller->getCurrentTemperature();
  if (abs(newCurrentTemp - currentTemp->getVal()) >= TEMP_THRESHOLD) {
    currentTemp->setVal(newCurrentTemp);
    #ifdef DEBUG_MODE
    if (abs(newCurrentTemp - lastOutputCurrentTemp) >= TEMP_THRESHOLD) {
      DEBUG_INFO_PRINT("[Device] 更新當前溫度：%.1f°C\n", newCurrentTemp);
      lastOutputCurrentTemp = newCurrentTemp;
    }
    #endif
  }
  
  // 獲取當前模式
  ThermostatMode newMode = controller->getCurrentMode();
  
  // 計算新的實際狀態
  int newCurrentState;
  if (newMode == ThermostatMode::AUTO) {
    // 在 AUTO 模式下，根據當前溫度和目標溫度決定實際狀態
    float targetTemp = controller->getTargetTemperature();
    float currentTemp = controller->getCurrentTemperature();
    if (currentTemp < targetTemp - 0.5) {  // 添加溫差閾值
      newCurrentState = static_cast<int>(ThermostatMode::HEAT);
    } else if (currentTemp > targetTemp + 0.5) {  // 添加溫差閾值
      newCurrentState = static_cast<int>(ThermostatMode::COOL);
    } else {
      newCurrentState = static_cast<int>(ThermostatMode::OFF);
    }
  } else {
    // 對於非 AUTO 模式，直接使用當前模式
    newCurrentState = static_cast<int>(newMode);
  }
  
  // 只在實��狀態發生變化時更新
  if (newCurrentState != currentMode->getVal()) {
    currentMode->setVal(newCurrentState);
    #ifdef DEBUG_MODE
    DEBUG_INFO_PRINT("[Device] 更新 HomeKit 當前模式：%d(%s)\n", 
                    newCurrentState, getHomeKitModeText(newCurrentState));
    #endif
  }
  
  // 確保目標模式和當前模式一致（除了 AUTO 模式）
  if (newMode != ThermostatMode::AUTO && static_cast<int>(newMode) != targetMode->getVal()) {
    targetMode->setVal(static_cast<int>(newMode));
  }
}