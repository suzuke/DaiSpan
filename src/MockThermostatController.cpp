#include "controller/MockThermostatController.h"

MockThermostatController::MockThermostatController(float initialTemp) 
    : power(false), 
      targetMode(0), 
      targetTemperature(initialTemp), 
      currentTemperature(initialTemp),
      simulatedRoomTemp(initialTemp),
      lastUpdateTime(0),
      isHeating(false),
      isCooling(false) {
    
    DEBUG_INFO_PRINT("[MockController] 模擬控制器初始化 - 初始溫度: %.1f°C\n", initialTemp);
}

bool MockThermostatController::setPower(bool on) {
    if (power != on) {
        power = on;
        DEBUG_INFO_PRINT("[MockController] 電源設置: %s\n", on ? "開啟" : "關閉");
        
        if (!on) {
            isHeating = false;
            isCooling = false;
        }
    }
    return true;
}

bool MockThermostatController::getPower() const {
    return power;
}

bool MockThermostatController::setTargetMode(uint8_t mode) {
    if (targetMode != mode) {
        targetMode = mode;
        DEBUG_INFO_PRINT("[MockController] 模式設置: %d(%s)\n", mode, getModeText(mode));
        
        // 重置加熱/制冷狀態
        isHeating = false;
        isCooling = false;
    }
    return true;
}

uint8_t MockThermostatController::getTargetMode() const {
    return targetMode;
}

bool MockThermostatController::setTargetTemperature(float temperature) {
    if (abs(targetTemperature - temperature) >= 0.1f) {
        targetTemperature = temperature;
        DEBUG_INFO_PRINT("[MockController] 目標溫度設置: %.1f°C\n", temperature);
    }
    return true;
}

float MockThermostatController::getTargetTemperature() const {
    return targetTemperature;
}

float MockThermostatController::getCurrentTemperature() const {
    return currentTemperature;
}

void MockThermostatController::update() {
    unsigned long currentTime = millis();
    
    if (currentTime - lastUpdateTime >= UPDATE_INTERVAL) {
        simulateTemperatureChange();
        lastUpdateTime = currentTime;
    }
}

void MockThermostatController::simulateTemperatureChange() {
    if (!power) {
        // 設備關閉時，溫度慢慢回到環境溫度
        if (currentTemperature > simulatedRoomTemp) {
            currentTemperature -= AMBIENT_DRIFT;
            if (currentTemperature < simulatedRoomTemp) {
                currentTemperature = simulatedRoomTemp;
            }
        } else if (currentTemperature < simulatedRoomTemp) {
            currentTemperature += AMBIENT_DRIFT;
            if (currentTemperature > simulatedRoomTemp) {
                currentTemperature = simulatedRoomTemp;
            }
        }
        isHeating = false;
        isCooling = false;
        return;
    }
    
    // 根據模式和目標溫度調整當前溫度
    float tempDiff = targetTemperature - currentTemperature;
    
    switch (targetMode) {
        case 1: // 制熱模式
            if (tempDiff > 0.2f) {
                currentTemperature += HEATING_EFFICIENCY;
                isHeating = true;
                isCooling = false;
                DEBUG_VERBOSE_PRINT("[MockController] 模擬加熱中... 當前: %.1f°C -> 目標: %.1f°C\n", 
                                  currentTemperature, targetTemperature);
            } else {
                isHeating = false;
            }
            break;
            
        case 2: // 制冷模式
            if (tempDiff < -0.2f) {
                currentTemperature -= COOLING_EFFICIENCY;
                isCooling = true;
                isHeating = false;
                DEBUG_VERBOSE_PRINT("[MockController] 模擬制冷中... 當前: %.1f°C -> 目標: %.1f°C\n", 
                                  currentTemperature, targetTemperature);
            } else {
                isCooling = false;
            }
            break;
            
        case 3: // 自動模式
            if (tempDiff > 0.2f) {
                currentTemperature += HEATING_EFFICIENCY;
                isHeating = true;
                isCooling = false;
            } else if (tempDiff < -0.2f) {
                currentTemperature -= COOLING_EFFICIENCY;
                isCooling = true;
                isHeating = false;
            } else {
                isHeating = false;
                isCooling = false;
            }
            break;
            
        default: // 關閉模式
            isHeating = false;
            isCooling = false;
            break;
    }
    
    // 添加一些隨機波動模擬真實環境
    static unsigned long lastNoiseTime = 0;
    if (millis() - lastNoiseTime > 10000) { // 每10秒加入小幅波動
        currentTemperature += (random(-10, 11) / 100.0f); // ±0.1°C波動
        lastNoiseTime = millis();
    }
    
    // 確保溫度在合理範圍內
    if (currentTemperature < 10.0f) currentTemperature = 10.0f;
    if (currentTemperature > 40.0f) currentTemperature = 40.0f;
}

void MockThermostatController::setSimulatedRoomTemp(float temp) {
    simulatedRoomTemp = temp;
    DEBUG_INFO_PRINT("[MockController] 模擬環境溫度設置: %.1f°C\n", temp);
}

float MockThermostatController::getSimulatedRoomTemp() const {
    return simulatedRoomTemp;
}

void MockThermostatController::setCurrentTemperature(float temp) {
    currentTemperature = temp;
    DEBUG_INFO_PRINT("[MockController] 手動設置當前溫度: %.1f°C\n", temp);
}

bool MockThermostatController::isSimulationHeating() const {
    return isHeating;
}

bool MockThermostatController::isSimulationCooling() const {
    return isCooling;
}

const char* MockThermostatController::getModeText(uint8_t mode) const {
    switch (mode) {
        case 0: return "關閉";
        case 1: return "制熱";
        case 2: return "制冷";
        case 3: return "自動";
        default: return "未知";
    }
}