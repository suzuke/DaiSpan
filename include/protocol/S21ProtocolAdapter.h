#pragma once

#include "IACProtocol.h"
#include "S21Protocol.h"
#include "S21Utils.h"
#include "../common/Debug.h"
#include "../common/ThermostatMode.h"

// S21協議適配器 - 將S21協議適配到通用IACProtocol介面
class S21ProtocolAdapter : public IACProtocol {
private:
    std::unique_ptr<S21Protocol> s21Protocol;
    ACStatus lastStatus;
    bool lastOperationSuccess;
    String lastError;
    
    // 協議支持的能力
    static constexpr float MIN_TEMPERATURE = 16.0f;
    static constexpr float MAX_TEMPERATURE = 30.0f;
    static const std::vector<uint8_t> SUPPORTED_MODES;
    static const std::vector<uint8_t> SUPPORTED_FAN_SPEEDS;
    
    // 內部輔助方法
    void setLastError(const char* error);
    bool validateTemperature(float temperature) const;
    bool validateMode(uint8_t mode) const;
    bool validateFanSpeed(uint8_t fanSpeed) const;
    
public:
    explicit S21ProtocolAdapter(std::unique_ptr<S21Protocol> protocol);
    
    // IACProtocol介面實現
    bool begin() override;
    
    // 核心控制操作
    bool setPowerAndMode(bool power, uint8_t mode, float temperature, uint8_t fanSpeed) override;
    bool setTemperature(float temperature) override;
    
    // 狀態查詢操作
    bool queryStatus(ACStatus& status) override;
    bool queryTemperature(float& temperature) override;
    
    // 協議能力查詢
    bool supportsMode(uint8_t mode) const override;
    bool supportsFanSpeed(uint8_t fanSpeed) const override;
    std::pair<float, float> getTemperatureRange() const override;
    std::vector<uint8_t> getSupportedModes() const override;
    std::vector<uint8_t> getSupportedFanSpeeds() const override;
    
    // 協議信息
    const char* getProtocolName() const override;
    const char* getProtocolVersion() const override;
    
    // 錯誤處理
    bool isLastOperationSuccessful() const override;
    const char* getLastError() const override;
    
    // S21特定方法（向後兼容）
    S21Protocol* getS21Protocol() { return s21Protocol.get(); }
    const S21Protocol* getS21Protocol() const { return s21Protocol.get(); }
};