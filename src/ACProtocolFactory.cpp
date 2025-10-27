#include "protocol/ACProtocolFactory.h"

// 協議類型名稱映射
const std::map<ACProtocolType, const char*> ACProtocolFactory::PROTOCOL_TYPE_NAMES = {
    {ACProtocolType::S21_DAIKIN, "S21 Daikin"},
    {ACProtocolType::MITSUBISHI_SERIAL, "Mitsubishi Serial"},
    {ACProtocolType::PANASONIC_SERIAL, "Panasonic Serial"},
    {ACProtocolType::HITACHI_SERIAL, "Hitachi Serial"}};

std::unique_ptr<IACProtocol> ACProtocolFactory::createProtocol(
    ACProtocolType type,
    HardwareSerial& serial) {
    
    DEBUG_INFO_PRINT("[ACFactory] 創建協議實例: 類型=%d (%s)\n", 
                      static_cast<int>(type), getProtocolTypeName(type));
    
    switch (type) {
        case ACProtocolType::S21_DAIKIN: {
            DEBUG_INFO_PRINT("[ACFactory] 初始化S21 Daikin協議\n");
            auto s21Protocol = std::make_unique<S21Protocol>(serial);
            return std::make_unique<S21ProtocolAdapter>(std::move(s21Protocol));
        }
        
        case ACProtocolType::MITSUBISHI_SERIAL:
            DEBUG_ERROR_PRINT("[ACFactory] Mitsubishi協議尚未實現\n");
            return nullptr;
            
        case ACProtocolType::PANASONIC_SERIAL:
            DEBUG_ERROR_PRINT("[ACFactory] Panasonic協議尚未實現\n");
            return nullptr;
            
        case ACProtocolType::HITACHI_SERIAL:
            DEBUG_ERROR_PRINT("[ACFactory] Hitachi協議尚未實現\n");
            return nullptr;            
        default:
            DEBUG_ERROR_PRINT("[ACFactory] 不支持的協議類型: %d\n", static_cast<int>(type));
            return nullptr;
    }
}

ACProtocolType ACProtocolFactory::detectProtocolType(HardwareSerial& serial) {
    DEBUG_INFO_PRINT("[ACFactory] 開始自動檢測協議類型\n");
    
    // 首先嘗試檢測S21協議（大金）
    if (detectS21Protocol(serial)) {
        DEBUG_INFO_PRINT("[ACFactory] 檢測到S21 Daikin協議\n");
        return ACProtocolType::S21_DAIKIN;
    }
    
    // 嘗試檢測三菱協議
    if (detectMitsubishiProtocol(serial)) {
        DEBUG_INFO_PRINT("[ACFactory] 檢測到Mitsubishi協議\n");
        return ACProtocolType::MITSUBISHI_SERIAL;
    }
    
    // 嘗試檢測松下協議
    if (detectPanasonicProtocol(serial)) {
        DEBUG_INFO_PRINT("[ACFactory] 檢測到Panasonic協議\n");
        return ACProtocolType::PANASONIC_SERIAL;
    }
    
    DEBUG_WARN_PRINT("[ACFactory] 無法檢測到已知的協議類型\n");
    return ACProtocolType::S21_DAIKIN; // 默認使用S21
}

std::vector<ACProtocolType> ACProtocolFactory::getSupportedProtocols() const {
    return {
        ACProtocolType::S21_DAIKIN        // 其他協議待實現時添加
        // ACProtocolType::MITSUBISHI_SERIAL,
        // ACProtocolType::PANASONIC_SERIAL,
        // ACProtocolType::HITACHI_SERIAL
    };
}

const char* ACProtocolFactory::getProtocolTypeName(ACProtocolType type) const {
    auto it = PROTOCOL_TYPE_NAMES.find(type);
    if (it != PROTOCOL_TYPE_NAMES.end()) {
        return it->second;
    }
    return "Unknown Protocol";
}

std::unique_ptr<ACProtocolFactory> ACProtocolFactory::createFactory() {
    DEBUG_INFO_PRINT("[ACFactory] 創建協議工廠實例\n");
    return std::make_unique<ACProtocolFactory>();
}

bool ACProtocolFactory::isProtocolTypeSupported(ACProtocolType type) {
    switch (type) {
        case ACProtocolType::S21_DAIKIN:
            return true;        case ACProtocolType::MITSUBISHI_SERIAL:
        case ACProtocolType::PANASONIC_SERIAL:
        case ACProtocolType::HITACHI_SERIAL:
            return false; // 待實現
        default:
            return false;
    }
}
// 私有協議檢測方法實現
bool ACProtocolFactory::detectS21Protocol(HardwareSerial& serial) {
    DEBUG_VERBOSE_PRINT("[ACFactory] 檢測S21協議...\n");
    
    // 嘗試創建S21協議實例並初始化
    auto s21Protocol = std::make_unique<S21Protocol>(serial);
    
    // 如果初始化成功，說明是S21協議
    if (s21Protocol->begin()) {
        DEBUG_VERBOSE_PRINT("[ACFactory] S21協議檢測成功\n");
        return true;
    }
    
    DEBUG_VERBOSE_PRINT("[ACFactory] S21協議檢測失敗\n");
    return false;
}

bool ACProtocolFactory::detectMitsubishiProtocol(HardwareSerial& serial) {
    DEBUG_VERBOSE_PRINT("[ACFactory] 檢測Mitsubishi協議...\n");
    
    // TODO: 實現三菱協議檢測邏輯
    // 這裡應該實現三菱空調的協議檢測邏輯
    // 例如發送特定的查詢命令並檢查回應格式
    
    DEBUG_VERBOSE_PRINT("[ACFactory] Mitsubishi協議檢測尚未實現\n");
    return false;
}

bool ACProtocolFactory::detectPanasonicProtocol(HardwareSerial& serial) {
    DEBUG_VERBOSE_PRINT("[ACFactory] 檢測Panasonic協議...\n");
    
    // TODO: 實現松下協議檢測邏輯
    // 這裡應該實現松下空調的協議檢測邏輯
    
    DEBUG_VERBOSE_PRINT("[ACFactory] Panasonic協議檢測尚未實現\n");
    return false;
}