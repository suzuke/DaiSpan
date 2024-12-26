#pragma once

#include "HomeSpan.h"
#include "IThermostatControl.h"

// HomeKit 恆溫器設備
struct ThermostatDevice : Service::Thermostat {
  SpanCharacteristic *currentTemp;
  SpanCharacteristic *targetTemp;
  SpanCharacteristic *currentMode;
  SpanCharacteristic *targetMode;
  
  IThermostatControl* controller;  // 使用介面指針
  
  // 建構函數
  ThermostatDevice(IThermostatControl* thermostatControl);
  
  // HomeSpan 回調函數
  boolean update() override;
  void loop() override;
}; 