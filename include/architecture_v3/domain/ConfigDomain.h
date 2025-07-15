#pragma once

#include "../core/Result.h"
#include <Preferences.h>
#include <functional>
#include <string>

namespace DaiSpan::Domain::Config {

/**
 * 強類型配置鍵
 */
template<typename T>
struct ConfigKey {
    const char* key;
    T defaultValue;
    std::function<bool(const T&)> validator;
    
    ConfigKey(const char* k, const T& defaultVal, 
              std::function<bool(const T&)> valid = [](const T&) { return true; })
        : key(k), defaultValue(defaultVal), validator(valid) {}
};

// 預定義配置鍵
namespace Keys {
    // WiFi 配置
    inline const auto WIFI_SSID = ConfigKey<String>("wifi.ssid", "", 
        [](const String& s) { return s.length() <= 32; });
    inline const auto WIFI_PASSWORD = ConfigKey<String>("wifi.password", "", 
        [](const String& s) { return s.length() <= 64; });
    
    // 恆溫器配置
    inline const auto THERMOSTAT_UPDATE_INTERVAL = ConfigKey<uint32_t>("thermostat.update_interval", 1000,
        [](uint32_t v) { return v >= 500 && v <= 10000; });
    inline const auto THERMOSTAT_MIN_TEMP = ConfigKey<float>("thermostat.min_temp", 16.0f,
        [](float v) { return v >= 0.0f && v <= 50.0f; });
    
    // HomeKit 配置
    inline const auto HOMEKIT_DEVICE_NAME = ConfigKey<String>("homekit.device_name", "DaiSpan Thermostat",
        [](const String& s) { return s.length() > 0 && s.length() <= 64; });
    inline const auto HOMEKIT_PAIRING_CODE = ConfigKey<String>("homekit.pairing_code", "11122333",
        [](const String& s) { return s.length() == 8; });
}

/**
 * 配置管理器
 */
class ConfigurationManager {
public:
    explicit ConfigurationManager(Preferences& prefs) : preferences_(prefs) {
        if (!preferences_.begin("daispan", false)) {
            // DEBUG_ERROR_PRINT("[Config] 無法初始化 NVS 存儲\n");
        }
    }
    
    ~ConfigurationManager() {
        preferences_.end();
    }
    
    /**
     * 獲取配置值
     */
    template<typename T>
    Core::Result<T> get(const ConfigKey<T>& key) const {
        if constexpr (std::is_same_v<T, String>) {
            String value = preferences_.getString(key.key, key.defaultValue);
            if (key.validator(value)) {
                return Core::Result<T>::success(value);
            }
            return Core::Result<T>::failure(std::string("Configuration validation failed: ") + key.key);
        } else if constexpr (std::is_same_v<T, uint32_t>) {
            uint32_t value = preferences_.getUInt(key.key, key.defaultValue);
            if (key.validator(value)) {
                return Core::Result<T>::success(value);
            }
            return Core::Result<T>::failure(std::string("Configuration validation failed: ") + key.key);
        } else if constexpr (std::is_same_v<T, float>) {
            float value = preferences_.getFloat(key.key, key.defaultValue);
            if (key.validator(value)) {
                return Core::Result<T>::success(value);
            }
            return Core::Result<T>::failure(std::string("Configuration validation failed: ") + key.key);
        } else {
            static_assert(false, "Unsupported configuration type");
        }
    }
    
    /**
     * 設定配置值
     */
    template<typename T>
    Core::Result<void> set(const ConfigKey<T>& key, const T& value) {
        if (!key.validator(value)) {
            return Core::Result<void>::failure(std::string("Value validation failed: ") + key.key);
        }
        
        if constexpr (std::is_same_v<T, String>) {
            if (preferences_.putString(key.key, value) == 0) {
                return Core::Result<void>::failure(std::string("Failed to store configuration: ") + key.key);
            }
        } else if constexpr (std::is_same_v<T, uint32_t>) {
            if (preferences_.putUInt(key.key, value) == 0) {
                return Core::Result<void>::failure(std::string("Failed to store configuration: ") + key.key);
            }
        } else if constexpr (std::is_same_v<T, float>) {
            if (preferences_.putFloat(key.key, value) == 0) {
                return Core::Result<void>::failure(std::string("Failed to store configuration: ") + key.key);
            }
        }
        
        return Core::Result<void>::success();
    }

private:
    Preferences& preferences_;
};

} // namespace DaiSpan::Domain::Config