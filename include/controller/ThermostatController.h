#pragma once

#include "IThermostatControl.h"
#include "../protocol/S21Protocol.h"
#include "../common/Debug.h"
#include "../common/ThermostatMode.h"

class ThermostatController : public IThermostatControl {
private:
    S21Protocol& protocol;
    bool power;
    uint8_t mode;
    float targetTemperature;
    float currentTemperature;
    unsigned long consecutiveErrors;
    unsigned long lastUpdateTime;

public:
    explicit ThermostatController(S21Protocol& p);
    
    // IThermostatControl interface implementation
    bool setPower(bool on) override;
    bool getPower() const override { return power; }
    
    bool setTargetMode(uint8_t newMode) override;
    uint8_t getTargetMode() const override { return convertACToHomeKitMode(mode, power); }
    
    bool setTargetTemperature(float temperature) override;
    float getTargetTemperature() const override { return targetTemperature; }
    
    float getCurrentTemperature() const override { return currentTemperature; }
    
    void update() override;
}; 