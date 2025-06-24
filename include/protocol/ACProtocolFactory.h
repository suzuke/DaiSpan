#pragma once

#include "IACProtocol.h"
#include "S21ProtocolAdapter.h"
#include "S21Protocol.h"
#include "../common/Debug.h"
#include <map>

// 協議工廠實現
class ACProtocolFactory : public IACProtocolFactory {
private:
    // 協議檢測超時時間
    static constexpr unsigned long DETECTION_TIMEOUT = 3000;
    
    // 協議類型名稱映射
    static const std::map<ACProtocolType, const char*> PROTOCOL_TYPE_NAMES;
    
    // 內部輔助方法
    bool detectS21Protocol(HardwareSerial& serial);
    bool detectMitsubishiProtocol(HardwareSerial& serial);
    bool detectPanasonicProtocol(HardwareSerial& serial);
    
public:
    ACProtocolFactory() = default;
    virtual ~ACProtocolFactory() = default;
    
    // IACProtocolFactory介面實現
    std::unique_ptr<IACProtocol> createProtocol(
        ACProtocolType type,
        HardwareSerial& serial
    ) override;
    
    ACProtocolType detectProtocolType(HardwareSerial& serial) override;
    
    std::vector<ACProtocolType> getSupportedProtocols() const override;
    
    const char* getProtocolTypeName(ACProtocolType type) const override;
    
    // 靜態工廠方法（便利函數）
    static std::unique_ptr<ACProtocolFactory> createFactory();
    
    // 協議檢測輔助方法
    static bool isProtocolTypeSupported(ACProtocolType type);
    static std::unique_ptr<IACProtocol> createMockProtocol();
};