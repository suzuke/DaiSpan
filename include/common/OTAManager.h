#pragma once

#include <ArduinoOTA.h>
#include <WiFi.h>
#include "Debug.h"
#include "Config.h"

class OTAManager {
private:
    ConfigManager& config;
    bool isEnabled;
    bool updateInProgress;
    String deviceHostname;
    String otaPassword;
    
    // OTA 配置常量
    static constexpr int OTA_PORT = 3232;
    static constexpr const char* OTA_PASSWORD_PREFIX = "DS";
    
public:
    OTAManager(ConfigManager& cfg) : 
        config(cfg), 
        isEnabled(false), 
        updateInProgress(false),
        deviceHostname(""),
        otaPassword("") {}
    
    // 生成基於MAC地址的唯一密碼
    String generateSecurePassword() {
        String mac = WiFi.macAddress();
        mac.replace(":", "");
        mac.toLowerCase();
        
        // 使用MAC地址的後6位加上前綴，創建8位密碼
        String password = OTA_PASSWORD_PREFIX + mac.substring(6);
        DEBUG_INFO_PRINT("[OTA] 生成設備專用密碼\n");
        return password;
    }
    
    // 初始化 OTA 服務
    bool begin() {
        if (!WiFi.isConnected()) {
            DEBUG_ERROR_PRINT("[OTA] WiFi 未連接，無法啟動 OTA\n");
            return false;
        }
        
        // 設置設備主機名
        deviceHostname = "DaiSpan-" + String(WiFi.macAddress());
        deviceHostname.replace(":", "");
        ArduinoOTA.setHostname(deviceHostname.c_str());
        
        // 生成並設置唯一密碼
        otaPassword = generateSecurePassword();
        
        // 設置 OTA 端口和密碼
        ArduinoOTA.setPort(OTA_PORT);
        ArduinoOTA.setPassword(otaPassword.c_str());
        
        DEBUG_INFO_PRINT("[OTA] 主機名: %s\n", deviceHostname.c_str());
        DEBUG_INFO_PRINT("[OTA] 密碼: %s\n", otaPassword.c_str());
        
        // 設置 OTA 事件回調
        ArduinoOTA.onStart([this]() {
            updateInProgress = true;
            String type;
            if (ArduinoOTA.getCommand() == U_FLASH) {
                type = "sketch";
            } else { // U_SPIFFS
                type = "filesystem";
            }
            DEBUG_INFO_PRINT("[OTA] 開始更新 %s\n", type.c_str());
        });
        
        ArduinoOTA.onEnd([this]() {
            updateInProgress = false;
            DEBUG_INFO_PRINT("[OTA] 更新完成\n");
        });
        
        ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
            static unsigned long lastPrint = 0;
            unsigned long now = millis();
            // 每秒更新一次進度
            if (now - lastPrint > 1000) {
                DEBUG_INFO_PRINT("[OTA] 進度: %u%% (%u/%u bytes)\n", 
                                (progress * 100) / total, progress, total);
                lastPrint = now;
            }
        });
        
        ArduinoOTA.onError([this](ota_error_t error) {
            updateInProgress = false;
            DEBUG_ERROR_PRINT("[OTA] 錯誤[%u]: ", error);
            if (error == OTA_AUTH_ERROR) {
                DEBUG_ERROR_PRINT("認證失敗\n");
            } else if (error == OTA_BEGIN_ERROR) {
                DEBUG_ERROR_PRINT("開始失敗\n");
            } else if (error == OTA_CONNECT_ERROR) {
                DEBUG_ERROR_PRINT("連接失敗\n");
            } else if (error == OTA_RECEIVE_ERROR) {
                DEBUG_ERROR_PRINT("接收失敗\n");
            } else if (error == OTA_END_ERROR) {
                DEBUG_ERROR_PRINT("結束失敗\n");
            }
        });
        
        ArduinoOTA.begin();
        isEnabled = true;
        
        DEBUG_INFO_PRINT("[OTA] OTA 服務已啟動\n");
        DEBUG_INFO_PRINT("主機名: %s\n", deviceHostname.c_str());
        DEBUG_INFO_PRINT("端口: %d\n", OTA_PORT);
        DEBUG_INFO_PRINT("IP: %s\n", WiFi.localIP().toString().c_str());
        
        return true;
    }
    
    // 處理 OTA 更新
    void handle() {
        if (isEnabled && WiFi.isConnected()) {
            ArduinoOTA.handle();
        }
    }
    
    // 停止 OTA 服務
    void end() {
        if (isEnabled) {
            ArduinoOTA.end();
            isEnabled = false;
            DEBUG_INFO_PRINT("[OTA] OTA 服務已停止\n");
        }
    }
    
    // 檢查是否正在更新
    bool isUpdating() const {
        return updateInProgress;
    }
    
    // 獲取狀態信息
    bool isOTAEnabled() const {
        return isEnabled;
    }
    
    String getHostname() const {
        return deviceHostname;
    }
    
    String getOTAPassword() const {
        return otaPassword;
    }
    
    // 設置 OTA 密碼
    void setPassword(const String& password) {
        otaPassword = password;
        if (isEnabled) {
            ArduinoOTA.setPassword(password.c_str());
        }
    }
    
    // 獲取 OTA 狀態 HTML
    String getStatusHTML() const {
        String html = "<div class=\"ota-status\">";
        html += "<h3>🔄 OTA 更新狀態</h3>";
        
        if (isEnabled) {
            html += "<p><span style=\"color: green;\">●</span> OTA 服務運行中</p>";
            html += "<p><strong>主機名:</strong> " + deviceHostname + "</p>";
            html += "<p><strong>端口:</strong> " + String(OTA_PORT) + "</p>";
            html += "<p><strong>IP:</strong> " + WiFi.localIP().toString() + "</p>";
            
            if (updateInProgress) {
                html += "<p><span style=\"color: orange;\">⏳</span> 正在更新中...</p>";
            }
        } else {
            html += "<p><span style=\"color: red;\">●</span> OTA 服務未啟動</p>";
        }
        
        html += "</div>";
        return html;
    }
};