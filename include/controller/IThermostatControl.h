#pragma once

#include <stdint.h>
#include <vector>
#include "../protocol/IACProtocol.h"

// 恆溫器控制介面
class IThermostatControl {
public:
  // 虛擬解構函數
  virtual ~IThermostatControl() = default;
  
  // 電源控制
  virtual bool setPower(bool on) = 0;
  virtual bool getPower() const = 0;
  
  // 模式控制
  virtual bool setTargetMode(uint8_t mode) = 0;
  virtual uint8_t getTargetMode() const = 0;
  
  // 溫度控制
  virtual bool setTargetTemperature(float temperature) = 0;
  virtual float getTargetTemperature() const = 0;
  virtual float getCurrentTemperature() const = 0;
  
  // 風量控制
  virtual bool setFanSpeed(uint8_t speed) = 0;
  virtual uint8_t getFanSpeed() const = 0;
  
  // 擺風控制
  virtual bool supportsSwing(IACProtocol::SwingAxis axis) const = 0;
  virtual bool setSwing(IACProtocol::SwingAxis axis, bool enabled) = 0;
  virtual bool getSwing(IACProtocol::SwingAxis axis) const = 0;

  virtual bool supportsSwingAngle(IACProtocol::SwingAxis axis) const = 0;
  virtual std::vector<int> getAvailableSwingAngles(IACProtocol::SwingAxis axis) const = 0;
  virtual bool setSwingAngle(IACProtocol::SwingAxis axis, int angleCode) = 0;
  virtual int getSwingAngle(IACProtocol::SwingAxis axis) const = 0;
  
  // 更新狀態
  virtual void update() = 0;
};
