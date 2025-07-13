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
    // 高性能定時系統
    struct OptimizedTimingSystem {
        // 合併定時器到單一結構，減少比較次數
        unsigned long nextPowerCheck;
        unsigned long nextPairingCheck;
        unsigned long nextHeartbeat;
        unsigned long nextWebServerHandle;
        unsigned long nextWiFiCheck;
        unsigned long homeKitReadyTime;
        
        // 狀態標誌
        bool webServerStartScheduled;
        bool homeKitStabilized;
        bool wasPairing;
        int webServerSkipCounter;
        uint32_t avgMemory;
        
        // 循環計數器優化 - 減少毫秒調用
        uint16_t loopCounter;
        uint16_t fastLoopDivider;
        
        OptimizedTimingSystem() : nextPowerCheck(0), nextPairingCheck(0), nextHeartbeat(0),
                                 nextWebServerHandle(0), nextWiFiCheck(0), homeKitReadyTime(0),
                                 webServerStartScheduled(false), homeKitStabilized(false),
                                 wasPairing(false), webServerSkipCounter(0), avgMemory(0),
                                 loopCounter(0), fastLoopDivider(100) {}
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
    void handleOptimizedTimingTasks(unsigned long currentTime);
    void handleESP32C3PowerManagement(unsigned long currentTime);
    void handleGlobalWiFiMonitoring(unsigned long currentTime);
    void handleOTAUpdates();
    void handleHomeKitMode(unsigned long currentTime);
    void handleConfigurationMode();
    void handleWebServerStartup(unsigned long currentTime);
    void handleHomeKitPairingDetection(unsigned long currentTime);
    void handleWebServerProcessing(unsigned long currentTime);
    void handlePeriodicTasks(unsigned long currentTime);
    void printHeartbeatInfo(unsigned long currentTime);
    void handleSmartWiFiPowerManagement();
    
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