#pragma once

#include <Arduino.h>
#include <memory>
#include <vector>
#include "../common/ThermostatMode.h"

// 空調狀態結構
struct ACStatus {
    bool power;
    uint8_t mode;           // AC內部模式
    float targetTemperature;
    float currentTemperature;
    uint8_t fanSpeed;
    bool isValid;           // 狀態是否有效
    
    ACStatus() : power(false), mode(0), targetTemperature(21.0f), 
                 currentTemperature(21.0f), fanSpeed(0), isValid(false) {}
};

// 通用空調協議介面
class IACProtocol {
public:
    virtual ~IACProtocol() = default;
    
    // 協議初始化
    virtual bool begin() = 0;
    
    // 核心控制操作
    virtual bool setPowerAndMode(bool power, uint8_t mode, float temperature, uint8_t fanSpeed) = 0;
    virtual bool setTemperature(float temperature) = 0;
    
    // 狀態查詢操作
    virtual bool queryStatus(ACStatus& status) = 0;
    virtual bool queryTemperature(float& temperature) = 0;
    
    // 協議能力查詢
    virtual bool supportsMode(uint8_t mode) const = 0;
    virtual bool supportsFanSpeed(uint8_t fanSpeed) const = 0;
    virtual std::pair<float, float> getTemperatureRange() const = 0;
    virtual std::vector<uint8_t> getSupportedModes() const = 0;
    virtual std::vector<uint8_t> getSupportedFanSpeeds() const = 0;
    
    // 協議信息
    virtual const char* getProtocolName() const = 0;
    virtual const char* getProtocolVersion() const = 0;
    
    // 錯誤處理
    virtual bool isLastOperationSuccessful() const = 0;
    virtual const char* getLastError() const = 0;
};

// 協議類型枚舉
enum class ACProtocolType {
    S21_DAIKIN = 1,
    MITSUBISHI_SERIAL = 2,
    PANASONIC_SERIAL = 3,
    HITACHI_SERIAL = 4,
    MOCK_PROTOCOL = 99
};

// 協議工廠介面
class IACProtocolFactory {
public:
    virtual ~IACProtocolFactory() = default;
    
    // 創建協議實例
    virtual std::unique_ptr<IACProtocol> createProtocol(
        ACProtocolType type,
        HardwareSerial& serial
    ) = 0;
    
    // 自動檢測協議類型
    virtual ACProtocolType detectProtocolType(HardwareSerial& serial) = 0;
    
    // 獲取支持的協議列表
    virtual std::vector<ACProtocolType> getSupportedProtocols() const = 0;
    
    // 協議類型名稱轉換
    virtual const char* getProtocolTypeName(ACProtocolType type) const = 0;
};