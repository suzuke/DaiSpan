#pragma once

#include "IThermostatControl.h"
#include "../common/Debug.h"

// 模擬恆溫器控制器 - 用於測試HomeKit功能而不需要真實空調
class MockThermostatController : public IThermostatControl {
private:
    bool power;
    uint8_t targetMode;
    float targetTemperature;
    float currentTemperature;
    float simulatedRoomTemp;
    uint8_t fanSpeed;
    unsigned long lastUpdateTime;
    bool isHeating;
    bool isCooling;
    
    // 模擬參數
    static constexpr float TEMP_CHANGE_RATE = 0.1f;  // 每次更新的溫度變化率
    static constexpr float HEATING_EFFICIENCY = 0.15f; // 加熱效率
    static constexpr float COOLING_EFFICIENCY = 0.12f; // 制冷效率
    static constexpr float AMBIENT_DRIFT = 0.02f;      // 環境溫度漂移
    static constexpr unsigned long UPDATE_INTERVAL = 2000; // 2秒更新一次

public:
    MockThermostatController(float initialTemp = 25.0f);
    
    // 實現 IThermostatControl 介面
    bool setPower(bool on) override;
    bool getPower() const override;
    
    bool setTargetMode(uint8_t mode) override;
    uint8_t getTargetMode() const override;
    
    bool setTargetTemperature(float temperature) override;
    float getTargetTemperature() const override;
    float getCurrentTemperature() const override;
    
    bool setFanSpeed(uint8_t speed) override;
    uint8_t getFanSpeed() const override;
    
    void update() override;
    
    // 模擬專用方法
    void setSimulatedRoomTemp(float temp);
    float getSimulatedRoomTemp() const;
    void setCurrentTemperature(float temp); // 用於手動設置當前溫度
    bool isSimulationHeating() const;
    bool isSimulationCooling() const;
    
private:
    void simulateTemperatureChange();
    const char* getModeText(uint8_t mode) const;
};