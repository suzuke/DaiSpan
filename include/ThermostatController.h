#pragma once

#include <Arduino.h>
#include "ThermostatMode.h"
#include "IThermostatControl.h"
#include "S21Protocol.h"

class ThermostatController : public IThermostatControl {
private:
  static constexpr float MIN_TEMP = 16.0;
  static constexpr float MAX_TEMP = 30.0;
  static constexpr unsigned long UPDATE_INTERVAL = 1000;  // 1秒更新一次
  static constexpr float TEMP_UPDATE_THRESHOLD = 0.1;    // 溫度變化閾值

  S21Protocol& protocol;
  float currentTemperature;
  float targetTemperature;
  ThermostatMode currentMode;
  ThermostatMode targetMode;
  unsigned long lastUpdateTime;
  unsigned long consecutiveErrors;  // 添加連續錯誤計數器

  bool isValidModeTransition(ThermostatMode newMode);
  bool shouldUpdate();
  bool updateCurrentTemp();
  bool updateCurrentMode();

public:
  explicit ThermostatController(S21Protocol& s21);
  virtual ~ThermostatController() = default;

  // IThermostatControl介面實作
  bool setTargetTemperature(float temp) override;
  float getCurrentTemperature() const override;
  float getTargetTemperature() const override;
  bool setTargetMode(ThermostatMode mode) override;
  ThermostatMode getCurrentMode() const override;
  ThermostatMode getTargetMode() const override;
  void loop() override;
}; 