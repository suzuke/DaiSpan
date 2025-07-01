#pragma once

#include <vector>
#include <memory>
#include "IS21Protocol.h"
#include "S21Utils.h"

// S21 協議變體定義 (Phase 3)
enum class S21ProtocolVariant {
    STANDARD = 0,        // 標準 Daikin S21
    DAIKIN_ENHANCED = 1, // 大金增強版
    MITSUBISHI = 2,      // 三菱電機變體
    PANASONIC = 3,       // 松下變體
    HITACHI = 4,         // 日立變體
    FUJITSU = 5,         // 富士通變體
    SHARP = 6,           // 夏普變體
    GREE = 7,            // 格力變體
    MIDEA = 8,           // 美的變體
    HAIER = 9,           // 海爾變體
    UNKNOWN = 255        // 未知變體
};

// 協議變體特性
struct S21ProtocolVariantInfo {
    S21ProtocolVariant variant;
    const char* name;
    const char* manufacturer;
    uint8_t checksumType;        // 校驗和類型（0=標準, 1=CRC, 2=XOR）
    uint8_t frameFormat;         // 幀格式（0=標準, 1=擴展）
    uint8_t temperatureFormat;   // 溫度格式
    uint8_t humidityFormat;      // 濕度格式
    bool hasExtendedCommands;    // 是否支援擴展命令
    bool hasCustomEncoding;      // 是否使用自定義編碼
    uint16_t manufacturerId;     // 製造商ID
    uint16_t protocolId;         // 協議ID
    
    S21ProtocolVariantInfo() : 
        variant(S21ProtocolVariant::UNKNOWN), name("Unknown"), manufacturer("Unknown"),
        checksumType(0), frameFormat(0), temperatureFormat(0), humidityFormat(0),
        hasExtendedCommands(false), hasCustomEncoding(false), 
        manufacturerId(0), protocolId(0) {}
};

// 協議變體適配器基類
class S21ProtocolVariantAdapter {
public:
    virtual ~S21ProtocolVariantAdapter() = default;
    
    // 變體特定的編碼/解碼方法
    virtual uint8_t calculateChecksum(const uint8_t* buffer, size_t len) const = 0;
    virtual bool validateFrame(const uint8_t* buffer, size_t len) const = 0;
    virtual float decodeTemperature(const uint8_t* data) const = 0;
    virtual uint8_t encodeTemperature(float temperature) const = 0;
    virtual float decodeHumidity(const uint8_t* data) const = 0;
    virtual uint8_t encodeHumidity(float humidity) const = 0;
    
    // 命令映射
    virtual bool mapCommand(char& cmd0, char& cmd1) const = 0;
    virtual bool mapResponse(char& cmd0, char& cmd1) const = 0;
    
    // 變體識別
    virtual bool detectVariant(const uint8_t* identityData, size_t len) const = 0;
    virtual S21ProtocolVariantInfo getVariantInfo() const = 0;
};

// 標準 Daikin 適配器
class DaikinStandardAdapter : public S21ProtocolVariantAdapter {
public:
    uint8_t calculateChecksum(const uint8_t* buffer, size_t len) const override {
        return s21_checksum(const_cast<uint8_t*>(buffer), len);
    }
    
    bool validateFrame(const uint8_t* buffer, size_t len) const override {
        return (len >= 5 && buffer[0] == STX && buffer[len-1] == ETX);
    }
    
    float decodeTemperature(const uint8_t* data) const override {
        return s21_decode_target_temp(data[0]);
    }
    
    uint8_t encodeTemperature(float temperature) const override {
        return s21_encode_target_temp(temperature);
    }
    
    float decodeHumidity(const uint8_t* data) const override {
        return s21_decode_humidity(data, 0);  // 標準格式
    }
    
    uint8_t encodeHumidity(float humidity) const override {
        uint8_t result;
        s21_encode_humidity(humidity, &result, 0);
        return result;
    }
    
    bool mapCommand(char& cmd0, char& cmd1) const override {
        // 標準映射，不需要轉換
        return true;
    }
    
    bool mapResponse(char& cmd0, char& cmd1) const override {
        // 標準映射，不需要轉換
        return true;
    }
    
    bool detectVariant(const uint8_t* identityData, size_t len) const override {
        // 檢查是否為標準 Daikin 協議
        return (len >= 2 && identityData[0] == 'D' && identityData[1] == 'K');
    }
    
    S21ProtocolVariantInfo getVariantInfo() const override {
        S21ProtocolVariantInfo info;
        info.variant = S21ProtocolVariant::STANDARD;
        info.name = "Standard Daikin S21";
        info.manufacturer = "Daikin";
        info.checksumType = 0;
        info.frameFormat = 0;
        info.temperatureFormat = 0;
        info.humidityFormat = 0;
        info.hasExtendedCommands = true;
        info.hasCustomEncoding = false;
        info.manufacturerId = 0x44; // 'D'
        info.protocolId = 0x4B;     // 'K'
        return info;
    }
};

// 大金增強版適配器
class DaikinEnhancedAdapter : public DaikinStandardAdapter {
public:
    float decodeTemperature(const uint8_t* data) const override {
        // 增強版支援更高精度和負溫度
        return s21_decode_temperature_enhanced(data, 2);  // 16位有符號格式
    }
    
    bool detectVariant(const uint8_t* identityData, size_t len) const override {
        return (len >= 3 && identityData[0] == 'D' && 
                identityData[1] == 'K' && identityData[2] == 'E');
    }
    
    S21ProtocolVariantInfo getVariantInfo() const override {
        S21ProtocolVariantInfo info = DaikinStandardAdapter::getVariantInfo();
        info.variant = S21ProtocolVariant::DAIKIN_ENHANCED;
        info.name = "Enhanced Daikin S21";
        info.temperatureFormat = 2;  // 16位有符號格式
        info.protocolId = 0x45;      // 'E'
        return info;
    }
};

// 三菱電機適配器
class MitsubishiAdapter : public S21ProtocolVariantAdapter {
public:
    uint8_t calculateChecksum(const uint8_t* buffer, size_t len) const override {
        // 三菱使用 XOR 校驗
        uint8_t checksum = 0;
        for (size_t i = 1; i < len - 2; i++) {
            checksum ^= buffer[i];
        }
        return checksum;
    }
    
    bool validateFrame(const uint8_t* buffer, size_t len) const override {
        return (len >= 5 && buffer[0] == 0x5A && buffer[len-1] == 0xA5);
    }
    
    float decodeTemperature(const uint8_t* data) const override {
        // 三菱溫度編碼：直接BCD格式
        return s21_decode_temperature_enhanced(data, 1);
    }
    
    uint8_t encodeTemperature(float temperature) const override {
        uint8_t data[2];
        s21_encode_temperature_enhanced(temperature, data, 1);
        return data[0];
    }
    
    float decodeHumidity(const uint8_t* data) const override {
        return s21_decode_humidity(data, 3);  // BCD 格式
    }
    
    uint8_t encodeHumidity(float humidity) const override {
        uint8_t result;
        s21_encode_humidity(humidity, &result, 3);
        return result;
    }
    
    bool mapCommand(char& cmd0, char& cmd1) const override {
        // 三菱命令映射
        if (cmd0 == 'F' && cmd1 == '1') {
            cmd0 = 'S'; cmd1 = '1'; // 狀態查詢
            return true;
        }
        if (cmd0 == 'D' && cmd1 == '1') {
            cmd0 = 'C'; cmd1 = '1'; // 控制命令
            return true;
        }
        return false;
    }
    
    bool mapResponse(char& cmd0, char& cmd1) const override {
        // 三菱回應映射
        if (cmd0 == 'R' && cmd1 == '1') {
            cmd0 = 'G'; cmd1 = '1';
            return true;
        }
        if (cmd0 == 'A' && cmd1 == '1') {
            cmd0 = 'H'; cmd1 = '1';
            return true;
        }
        return false;
    }
    
    bool detectVariant(const uint8_t* identityData, size_t len) const override {
        return (len >= 2 && identityData[0] == 'M' && identityData[1] == 'E');
    }
    
    S21ProtocolVariantInfo getVariantInfo() const override {
        S21ProtocolVariantInfo info;
        info.variant = S21ProtocolVariant::MITSUBISHI;
        info.name = "Mitsubishi Electric";
        info.manufacturer = "Mitsubishi";
        info.checksumType = 2;  // XOR
        info.frameFormat = 1;   // 自定義幀格式
        info.temperatureFormat = 1;  // BCD
        info.humidityFormat = 3;     // BCD
        info.hasExtendedCommands = false;
        info.hasCustomEncoding = true;
        info.manufacturerId = 0x4D;  // 'M'
        info.protocolId = 0x45;      // 'E'
        return info;
    }
};

// 協議變體檢測器
class S21ProtocolVariantDetector {
private:
    std::vector<std::unique_ptr<S21ProtocolVariantAdapter>> adapters;
    
public:
    S21ProtocolVariantDetector() {
        // 註冊所有支援的適配器
        adapters.push_back(std::make_unique<DaikinStandardAdapter>());
        adapters.push_back(std::make_unique<DaikinEnhancedAdapter>());
        adapters.push_back(std::make_unique<MitsubishiAdapter>());
    }
    
    S21ProtocolVariant detectVariant(const uint8_t* identityData, size_t len) {
        for (const auto& adapter : adapters) {
            if (adapter->detectVariant(identityData, len)) {
                return adapter->getVariantInfo().variant;
            }
        }
        return S21ProtocolVariant::UNKNOWN;
    }
    
    S21ProtocolVariantAdapter* getAdapter(S21ProtocolVariant variant) {
        for (const auto& adapter : adapters) {
            if (adapter->getVariantInfo().variant == variant) {
                return adapter.get();
            }
        }
        return nullptr;
    }
    
    std::vector<S21ProtocolVariantInfo> getSupportedVariants() const {
        std::vector<S21ProtocolVariantInfo> variants;
        for (const auto& adapter : adapters) {
            variants.push_back(adapter->getVariantInfo());
        }
        return variants;
    }
};