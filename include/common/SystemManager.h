#pragma once

#include <Arduino.h>
#include "WiFi.h"
#include "WebServer.h"
#include "ArduinoOTA.h"

// 前向宣告
class ConfigManager;
class WiFiManager;
class OTAManager;
class IThermostatControl;
class MockThermostatController;
class ThermostatDevice;

/**
 * 系統管理器類別
 * 負責協調和管理主迴圈中的各種系統組件
 */
class SystemManager {
private:
    // 系統狀態
    struct SystemState {
        unsigned long lastLoopTime;
        unsigned long lastPowerCheck;
        unsigned long lastOTAHandle;
        unsigned long lastPairingCheck;
        unsigned long lastWebServerHandle;
        unsigned long homeKitReadyTime;
        
        bool webServerStartScheduled;
        bool homeKitStabilized;
        bool wasPairing;
        int pairingDetectionCounter;
        int webServerSkipCounter;
        uint32_t avgMemory;
        
        SystemState() : lastLoopTime(0), lastPowerCheck(0), lastOTAHandle(0),
                       lastPairingCheck(0), lastWebServerHandle(0), homeKitReadyTime(0),
                       webServerStartScheduled(false), homeKitStabilized(false),
                       wasPairing(false), pairingDetectionCounter(0), 
                       webServerSkipCounter(0), avgMemory(0) {}
    } state;
    
    // 系統組件引用
    ConfigManager& configManager;
    WiFiManager*& wifiManager;
    WebServer*& webServer;
    IThermostatControl*& thermostatController;
    MockThermostatController*& mockController;
    ThermostatDevice*& thermostatDevice;
    
    // 系統狀態標誌
    bool& deviceInitialized;
    bool& homeKitInitialized;
    bool& monitoringEnabled;
    bool& homeKitPairingActive;
    
    // 私有方法
    void handleESP32C3PowerManagement(unsigned long currentTime);
    void handleOTAUpdates();
    void handleHomeKitMode(unsigned long currentTime);
    void handleConfigurationMode();
    void handleWebServerStartup(unsigned long currentTime);
    void handleHomeKitPairingDetection(unsigned long currentTime);
    void handleWebServerProcessing(unsigned long currentTime);
    void handlePeriodicTasks(unsigned long currentTime);
    void printHeartbeatInfo(unsigned long currentTime);
    
    // 輔助方法
    bool shouldStartWebServer(unsigned long currentTime) const;
    unsigned long calculateWebServerInterval(uint32_t freeMemory);
    bool shouldSkipWebServerProcessing() const;
    void updatePairingDetection(uint32_t currentMemory);
    
public:
    SystemManager(ConfigManager& config, WiFiManager*& wifi, WebServer*& web,
                 IThermostatControl*& controller, MockThermostatController*& mock,
                 ThermostatDevice*& device, bool& devInit, bool& hkInit, 
                 bool& monitoring, bool& pairing);
    
    /**
     * 主迴圈處理函式
     * 替代原本的 loop() 函式邏輯
     */
    void processMainLoop();
    
    /**
     * 重置系統狀態
     */
    void resetState();
    
    /**
     * 獲取系統運行統計
     */
    void getSystemStats(String& mode, String& wifiStatus, String& deviceStatus, String& ipAddress);
    
    /**
     * 檢查是否需要啟動 WebServer 監控
     */
    bool shouldStartMonitoring() const;
};