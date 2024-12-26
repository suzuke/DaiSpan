#pragma once

#include <Arduino.h>
#include "ThermostatMode.h"
#include "S21Protocol.h"

class S21Controller {
private:
    S21Protocol& protocol;
    
    // 當前狀態
    float currentTemp;
    float targetTemp;
    ThermostatMode currentMode;
    
    // 更新狀態
    bool updateCurrentTemp();
    bool updateCurrentMode();

public:
    S21Controller(S21Protocol& p);
    
    // 基本操作
    bool begin();
    bool update();
    
    // 溫度控制
    bool setTargetTemp(float temp);
    float getCurrentTemp() const { return currentTemp; }
    float getTargetTemp() const { return targetTemp; }
    
    // 模式控制
    bool setTargetMode(ThermostatMode mode);
    ThermostatMode getCurrentMode() const { return currentMode; }
}; 