#pragma once

#include "ThermostatMode.h"

// 恆溫器控制介面
class IThermostatControl {
public:
  // 虛擬解構函數
  virtual ~IThermostatControl() = default;
  
  // 溫度控制
  virtual bool setTargetTemperature(float temp) = 0;
  virtual float getCurrentTemperature() const = 0;
  virtual float getTargetTemperature() const = 0;
  
  // 模式控制
  virtual bool setTargetMode(ThermostatMode mode) = 0;
  virtual ThermostatMode getCurrentMode() const = 0;
  virtual ThermostatMode getTargetMode() const = 0;
  
  // 週期性運作
  virtual void loop() = 0;
};
