#pragma once

#include "IThermostatControl.h"
#include "../protocol/IACProtocol.h"
#include "../common/Debug.h"
#include "../common/ThermostatMode.h"
#include <memory>

// 重構後的通用恆溫器控制器
// 使用IACProtocol抽象介面，支持多種空調協議
class ThermostatController : public IThermostatControl {
private:
    std::unique_ptr<IACProtocol> protocol;
    bool power;
    uint8_t mode;
    float targetTemperature;
    float currentTemperature;
    uint8_t fanSpeed;
    unsigned long consecutiveErrors;
    unsigned long lastUpdateTime;
    unsigned long lastSuccessfulUpdate;
    
    // 用戶互動追蹤，防止設置被queryStatus覆蓋
    unsigned long lastFanSpeedSetTime;
    uint8_t lastUserFanSpeed;
    unsigned long lastModeSetTime;
    uint8_t lastUserMode;
    
    // 高性能錯誤處理和重試邏輯
    static constexpr unsigned long MAX_CONSECUTIVE_ERRORS = 10;     // 增加錯誤閾值，避免過早進入恢復模式
    static constexpr unsigned long ERROR_RECOVERY_INTERVAL = 30000; // 30秒恢復間隔
    static constexpr unsigned long UPDATE_INTERVAL = 6000;         // 6秒（提高查詢頻率但保持合理）
    
    // 內部輔助方法
    bool handleProtocolError(const char* operation);
    bool isInErrorRecoveryMode() const;
    void resetErrorCount();
    
public:
    // 構造函數使用協議實例
    explicit ThermostatController(std::unique_ptr<IACProtocol> p);
    
    // 移動構造函數和賦值操作符
    ThermostatController(ThermostatController&& other) noexcept;
    ThermostatController& operator=(ThermostatController&& other) noexcept;
    
    // 禁用複製構造和賦值
    ThermostatController(const ThermostatController&) = delete;
    ThermostatController& operator=(const ThermostatController&) = delete;
    
    // 解構函數
    virtual ~ThermostatController() = default;
    
    // IThermostatControl interface implementation
    bool setPower(bool on) override;
    bool getPower() const override { return power; }
    
    bool setTargetMode(uint8_t newMode) override;
    uint8_t getTargetMode() const override { return convertACToHomeKitMode(mode, power); }
    
    bool setTargetTemperature(float temperature) override;
    float getTargetTemperature() const override { return targetTemperature; }
    
    float getCurrentTemperature() const override { return currentTemperature; }
    
    bool setFanSpeed(uint8_t speed) override;
    uint8_t getFanSpeed() const override { return fanSpeed; }
    
    void update() override;
    
    // 協議相關查詢方法
    bool supportsMode(uint8_t mode) const;
    bool supportsFanSpeed(uint8_t fanSpeed) const;
    std::pair<float, float> getTemperatureRange() const;
    const char* getProtocolName() const;
    const char* getProtocolVersion() const;
    
    // 狀態和診斷方法
    unsigned long getConsecutiveErrors() const { return consecutiveErrors; }
    unsigned long getLastUpdateTime() const { return lastUpdateTime; }
    bool isProtocolHealthy() const { return consecutiveErrors < MAX_CONSECUTIVE_ERRORS; }
    void forceResetErrorState() { consecutiveErrors = 0; lastSuccessfulUpdate = millis(); }
    
    // 獲取底層協議實例（用於特殊操作）
    IACProtocol* getProtocol() const { return protocol.get(); }
};