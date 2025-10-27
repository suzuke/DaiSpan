#pragma once

#include <Arduino.h>
#include "WiFi.h"
#include "WebServer.h"
#include "core/Scheduler.h"

// 前向宣告
class ConfigManager;
class WiFiManager;
class IThermostatControl;
class ThermostatDevice;

/**
 * 系統管理器類別
 * 負責協調和管理主迴圈中的各種系統組件
 */
class SystemManager {
private:
    enum class MemoryPressure {
        Normal,
        Tight,
        Critical
    };

    // 高性能定時系統
    struct RuntimeState {
        unsigned long homeKitReadyTime = 0;
        bool homeKitStabilized = false;
        bool wasPairing = false;
        uint32_t avgMemory = 0;
        MemoryPressure memoryPressure = MemoryPressure::Normal;
        uint16_t lastTaskCount = 0;
        uint32_t lastSchedulerDurationUs = 0;
    } state;
    
    DaiSpan::Core::Scheduler scheduler;
    
    // 系統組件引用
    ConfigManager& configManager;
    WiFiManager*& wifiManager;
    WebServer*& webServer;
    IThermostatControl*& thermostatController;
    ThermostatDevice*& thermostatDevice;
    
    // 系統狀態標誌
    bool& deviceInitialized;
    bool& homeKitInitialized;
    bool& monitoringEnabled;
    bool& homeKitPairingActive;
    
    // 私有方法
    void handleGlobalWiFiMonitoring(unsigned long currentTime);
    void handleHomeKitMode(unsigned long currentTime);
    void handleConfigurationMode();
    void handleHomeKitPairingDetection(unsigned long currentTime);
    void printHeartbeatInfo(unsigned long currentTime);
    void handleSmartWiFiPowerManagement();
    void registerTasks();
    
    // 輔助方法
    void updatePairingDetection(uint32_t currentMemory);
    
public:
    SystemManager(ConfigManager& config, WiFiManager*& wifi, WebServer*& web,
                 IThermostatControl*& controller,
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

    uint32_t getAverageMemory() const { return state.avgMemory; }
    String getMemoryPressureText() const;
    uint16_t getSchedulerTaskCount() const { return state.lastTaskCount; }
    uint32_t getSchedulerDurationUs() const { return state.lastSchedulerDurationUs; }
    bool isMemoryCritical() const { return state.memoryPressure == MemoryPressure::Critical; }
    uint8_t getMemoryPressureLevel() const;
}; 
