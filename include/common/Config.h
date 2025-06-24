#pragma once

#include "Preferences.h"
#include "Debug.h"

// 配置常量
namespace ConfigConstants {
    // WiFi 配置
    static constexpr const char* WIFI_SSID_KEY = "wifi_ssid";
    static constexpr const char* WIFI_PASSWORD_KEY = "wifi_password";
    
    // HomeKit 配置
    static constexpr const char* HOMEKIT_PAIRING_CODE_KEY = "hk_pair_code";
    static constexpr const char* HOMEKIT_DEVICE_NAME_KEY = "hk_device_name";
    static constexpr const char* HOMEKIT_QR_ID_KEY = "hk_qr_id";
    
    // 系統配置
    static constexpr const char* UPDATE_INTERVAL_KEY = "update_interval";
    static constexpr const char* HEARTBEAT_INTERVAL_KEY = "heartbeat_interval";
    static constexpr const char* SERIAL_BAUD_KEY = "serial_baud";
    static constexpr const char* SIMULATION_MODE_KEY = "simulation_mode";
    
    // 溫度配置
    static constexpr const char* MIN_TEMP_KEY = "min_temp";
    static constexpr const char* MAX_TEMP_KEY = "max_temp";
    static constexpr const char* TEMP_STEP_KEY = "temp_step";
    static constexpr const char* TEMP_THRESHOLD_KEY = "temp_threshold";
    
    // 默認值 - 用於指示未配置狀態
    static constexpr const char* DEFAULT_WIFI_SSID = "UNCONFIGURED_SSID";
    static constexpr const char* DEFAULT_WIFI_PASSWORD = "UNCONFIGURED_PASSWORD";
    static constexpr const char* DEFAULT_PAIRING_CODE = "11122333";
    static constexpr const char* DEFAULT_DEVICE_NAME = "智能恆溫器";
    static constexpr const char* DEFAULT_QR_ID_STRING = "HSPN";
    static constexpr unsigned long DEFAULT_UPDATE_INTERVAL = 5000;
    static constexpr unsigned long DEFAULT_HEARTBEAT_INTERVAL = 30000;
    static constexpr unsigned long DEFAULT_SERIAL_BAUD = 2400;
    static constexpr bool DEFAULT_SIMULATION_MODE = false;
    static constexpr float DEFAULT_MIN_TEMP = 16.0f;
    static constexpr float DEFAULT_MAX_TEMP = 30.0f;
    static constexpr float DEFAULT_TEMP_STEP = 0.5f;
    static constexpr float DEFAULT_TEMP_THRESHOLD = 0.2f;
}

class ConfigManager {
private:
    Preferences preferences;
    bool initialized;
    
public:
    ConfigManager() : initialized(false) {}
    
    bool begin(const char* namespace_name = "daispan") {
        if (initialized) {
            return true;
        }
        
        bool success = preferences.begin(namespace_name, false);
        if (success) {
            initialized = true;
            DEBUG_INFO_PRINT("[Config] 配置管理器初始化成功\n");
            loadDefaults();
        } else {
            DEBUG_ERROR_PRINT("[Config] 配置管理器初始化失敗\n");
        }
        return success;
    }
    
    void end() {
        if (initialized) {
            preferences.end();
            initialized = false;
        }
    }
    
    // WiFi 配置
    String getWiFiSSID() {
        return preferences.getString(ConfigConstants::WIFI_SSID_KEY, ConfigConstants::DEFAULT_WIFI_SSID);
    }
    
    String getWiFiPassword() {
        return preferences.getString(ConfigConstants::WIFI_PASSWORD_KEY, ConfigConstants::DEFAULT_WIFI_PASSWORD);
    }
    
    bool setWiFiCredentials(const String& ssid, const String& password) {
        bool success = preferences.putString(ConfigConstants::WIFI_SSID_KEY, ssid);
        success &= preferences.putString(ConfigConstants::WIFI_PASSWORD_KEY, password);
        if (success) {
            DEBUG_INFO_PRINT("[Config] WiFi 憑證已保存\n");
        }
        return success;
    }
    
    bool isWiFiConfigured() {
        String ssid = getWiFiSSID();
        return (ssid != ConfigConstants::DEFAULT_WIFI_SSID && 
                ssid != "YourWiFiSSID" && 
                ssid.length() > 0);
    }
    
    // HomeKit 配置
    String getHomeKitPairingCode() {
        return preferences.getString(ConfigConstants::HOMEKIT_PAIRING_CODE_KEY, ConfigConstants::DEFAULT_PAIRING_CODE);
    }
    
    String getHomeKitDeviceName() {
        return preferences.getString(ConfigConstants::HOMEKIT_DEVICE_NAME_KEY, ConfigConstants::DEFAULT_DEVICE_NAME);
    }
    
    String getHomeKitQRID() {
        return preferences.getString(ConfigConstants::HOMEKIT_QR_ID_KEY, ConfigConstants::DEFAULT_QR_ID_STRING);
    }
    
    bool setHomeKitConfig(const String& pairingCode, const String& deviceName, const String& qrId) {
        bool success = preferences.putString(ConfigConstants::HOMEKIT_PAIRING_CODE_KEY, pairingCode);
        success &= preferences.putString(ConfigConstants::HOMEKIT_DEVICE_NAME_KEY, deviceName);
        success &= preferences.putString(ConfigConstants::HOMEKIT_QR_ID_KEY, qrId);
        return success;
    }
    
    // 系統配置
    unsigned long getUpdateInterval() {
        return preferences.getULong(ConfigConstants::UPDATE_INTERVAL_KEY, ConfigConstants::DEFAULT_UPDATE_INTERVAL);
    }
    
    unsigned long getHeartbeatInterval() {
        return preferences.getULong(ConfigConstants::HEARTBEAT_INTERVAL_KEY, ConfigConstants::DEFAULT_HEARTBEAT_INTERVAL);
    }
    
    unsigned long getSerialBaud() {
        return preferences.getULong(ConfigConstants::SERIAL_BAUD_KEY, ConfigConstants::DEFAULT_SERIAL_BAUD);
    }
    
    bool setSystemConfig(unsigned long updateInterval, unsigned long heartbeatInterval, unsigned long serialBaud) {
        bool success = preferences.putULong(ConfigConstants::UPDATE_INTERVAL_KEY, updateInterval);
        success &= preferences.putULong(ConfigConstants::HEARTBEAT_INTERVAL_KEY, heartbeatInterval);
        success &= preferences.putULong(ConfigConstants::SERIAL_BAUD_KEY, serialBaud);
        return success;
    }
    
    // 模擬模式配置
    bool getSimulationMode() {
        return preferences.getBool(ConfigConstants::SIMULATION_MODE_KEY, ConfigConstants::DEFAULT_SIMULATION_MODE);
    }
    
    bool setSimulationMode(bool enabled) {
        bool success = preferences.putBool(ConfigConstants::SIMULATION_MODE_KEY, enabled);
        if (success) {
            DEBUG_INFO_PRINT("[Config] 模擬模式設置: %s\n", enabled ? "啟用" : "停用");
        }
        return success;
    }
    
    // 溫度配置
    float getMinTemp() {
        return preferences.getFloat(ConfigConstants::MIN_TEMP_KEY, ConfigConstants::DEFAULT_MIN_TEMP);
    }
    
    float getMaxTemp() {
        return preferences.getFloat(ConfigConstants::MAX_TEMP_KEY, ConfigConstants::DEFAULT_MAX_TEMP);
    }
    
    float getTempStep() {
        return preferences.getFloat(ConfigConstants::TEMP_STEP_KEY, ConfigConstants::DEFAULT_TEMP_STEP);
    }
    
    float getTempThreshold() {
        return preferences.getFloat(ConfigConstants::TEMP_THRESHOLD_KEY, ConfigConstants::DEFAULT_TEMP_THRESHOLD);
    }
    
    bool setTempConfig(float minTemp, float maxTemp, float tempStep, float tempThreshold) {
        bool success = preferences.putFloat(ConfigConstants::MIN_TEMP_KEY, minTemp);
        success &= preferences.putFloat(ConfigConstants::MAX_TEMP_KEY, maxTemp);
        success &= preferences.putFloat(ConfigConstants::TEMP_STEP_KEY, tempStep);
        success &= preferences.putFloat(ConfigConstants::TEMP_THRESHOLD_KEY, tempThreshold);
        return success;
    }
    
    // 重置為默認值
    bool resetToDefaults() {
        preferences.clear();
        loadDefaults();
        DEBUG_INFO_PRINT("[Config] 配置已重置為默認值\n");
        return true;
    }
    
    // 清除 WiFi 配置，強制進入 AP 模式
    bool clearWiFiConfig() {
        bool success = preferences.remove(ConfigConstants::WIFI_SSID_KEY);
        success &= preferences.remove(ConfigConstants::WIFI_PASSWORD_KEY);
        if (success) {
            DEBUG_INFO_PRINT("[Config] WiFi 配置已清除\n");
        }
        return success;
    }
    
    // 打印當前配置
    void printConfig() {
        DEBUG_INFO_PRINT("========== 當前配置 ==========\n");
        DEBUG_INFO_PRINT("WiFi SSID: %s\n", getWiFiSSID().c_str());
        DEBUG_INFO_PRINT("HomeKit 配對碼: %s\n", getHomeKitPairingCode().c_str());
        DEBUG_INFO_PRINT("設備名稱: %s\n", getHomeKitDeviceName().c_str());
        DEBUG_INFO_PRINT("更新間隔: %lu ms\n", getUpdateInterval());
        DEBUG_INFO_PRINT("心跳間隔: %lu ms\n", getHeartbeatInterval());
        DEBUG_INFO_PRINT("溫度範圍: %.1f°C - %.1f°C\n", getMinTemp(), getMaxTemp());
        DEBUG_INFO_PRINT("==============================\n");
    }
    
private:
    void loadDefaults() {
        // 不自動設置 WiFi 憑證，讓系統檢測到未配置狀態
        // WiFi 憑證將通過 AP 配置模式設置
        if (!preferences.isKey(ConfigConstants::HOMEKIT_PAIRING_CODE_KEY)) {
            setHomeKitConfig(ConfigConstants::DEFAULT_PAIRING_CODE, 
                           ConfigConstants::DEFAULT_DEVICE_NAME, 
                           ConfigConstants::DEFAULT_QR_ID_STRING);
        }
        if (!preferences.isKey(ConfigConstants::UPDATE_INTERVAL_KEY)) {
            setSystemConfig(ConfigConstants::DEFAULT_UPDATE_INTERVAL,
                          ConfigConstants::DEFAULT_HEARTBEAT_INTERVAL,
                          ConfigConstants::DEFAULT_SERIAL_BAUD);
        }
        if (!preferences.isKey(ConfigConstants::MIN_TEMP_KEY)) {
            setTempConfig(ConfigConstants::DEFAULT_MIN_TEMP,
                        ConfigConstants::DEFAULT_MAX_TEMP,
                        ConfigConstants::DEFAULT_TEMP_STEP,
                        ConfigConstants::DEFAULT_TEMP_THRESHOLD);
        }
    }
};