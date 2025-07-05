#include "common/SystemManager.h"
#include "common/Config.h"
#include "controller/IThermostatControl.h"
#include "controller/MockThermostatController.h"
#include "device/ThermostatDevice.h"
#include "common/Debug.h"
#include "HomeSpan.h"

// 前向宣告避免包含問題的頭文件
class WiFiManager;
class OTAManager;

// 系統常數
static constexpr unsigned long POWER_CHECK_INTERVAL = 30000;     // ESP32-C3 功率檢查間隔
static constexpr unsigned long OTA_HANDLE_INTERVAL = 100;        // OTA 處理間隔
static constexpr unsigned long PAIRING_CHECK_INTERVAL = 5000;    // 配對檢測間隔
static constexpr unsigned long WEBSERVER_STARTUP_DELAY = 5000;   // WebServer 啟動延遲
static constexpr unsigned long SYSTEM_HEARTBEAT_INTERVAL = 30000; // 系統心跳間隔

// 記憶體閾值
static constexpr uint32_t MEMORY_DROP_THRESHOLD = 20000;         // 記憶體下降閾值
static constexpr uint32_t MEMORY_TIGHT_THRESHOLD = 60000;        // 記憶體緊張閾值
static constexpr uint32_t MEMORY_MEDIUM_THRESHOLD = 80000;       // 記憶體中等閾值

SystemManager::SystemManager(ConfigManager& config, WiFiManager*& wifi, WebServer*& web,
                           IThermostatControl*& controller, MockThermostatController*& mock,
                           ThermostatDevice*& device, bool& devInit, bool& hkInit, 
                           bool& monitoring, bool& pairing)
    : configManager(config), wifiManager(wifi), webServer(web),
      thermostatController(controller), mockController(mock), thermostatDevice(device),
      deviceInitialized(devInit), homeKitInitialized(hkInit), 
      monitoringEnabled(monitoring), homeKitPairingActive(pairing) {
    
    state.avgMemory = ESP.getFreeHeap();
    DEBUG_INFO_PRINT("[SystemManager] 初始化完成\n");
}

void SystemManager::processMainLoop() {
    unsigned long currentTime = millis();
    
    // ESP32-C3 特定的 WiFi 功率管理
    handleESP32C3PowerManagement(currentTime);
    
    // 高優先級：OTA 更新處理
    handleOTAUpdates();
    
    // 根據系統模式處理不同邏輯
    if (homeKitInitialized) {
        handleHomeKitMode(currentTime);
    }
    // 配置模式處理已移到main.cpp以避免依賴問題
    
    // 定期任務處理
    handlePeriodicTasks(currentTime);
}

void SystemManager::handleESP32C3PowerManagement(unsigned long currentTime) {
    #if defined(ESP32C3_SUPER_MINI)
    if (currentTime - state.lastPowerCheck >= POWER_CHECK_INTERVAL) {
        state.lastPowerCheck = currentTime;
        
        if (WiFi.status() == WL_DISCONNECTED && WiFi.getTxPower() != WIFI_POWER_8_5dBm) {
            WiFi.setTxPower(WIFI_POWER_8_5dBm);
            DEBUG_VERBOSE_PRINT("[SystemManager] ESP32-C3 調整WiFi功率為節能模式\n");
        }
    }
    #endif
}

void SystemManager::handleOTAUpdates() {
    if (WiFi.status() == WL_CONNECTED) {
        ArduinoOTA.handle();
    }
}

void SystemManager::handleHomeKitMode(unsigned long currentTime) {
    // WebServer 啟動管理
    handleWebServerStartup(currentTime);
    
    // HomeKit 核心處理（最高優先級）
    homeSpan.poll();
    
    // HomeKit 配對狀態檢測
    handleHomeKitPairingDetection(currentTime);
    
    // WebServer 處理（智能頻率控制）
    handleWebServerProcessing(currentTime);
    
    // OTA 處理（低優先級，限制頻率）
    if (currentTime - state.lastOTAHandle >= OTA_HANDLE_INTERVAL) {
        ArduinoOTA.handle();
        state.lastOTAHandle = currentTime;
    }
}

void SystemManager::handleWebServerStartup(unsigned long currentTime) {
    if (!state.webServerStartScheduled && !monitoringEnabled) {
        state.homeKitReadyTime = currentTime;
        state.webServerStartScheduled = true;
        DEBUG_INFO_PRINT("[SystemManager] WebServer將在HomeKit穩定後啟動（延遲%d秒）\n", 
                         WEBSERVER_STARTUP_DELAY / 1000);
    }
    
    if (shouldStartWebServer(currentTime)) {
        DEBUG_INFO_PRINT("[SystemManager] HomeKit已穩定，開始啟動WebServer\n");
        // 設置標誌，讓外部程式知道需要啟動監控
        // 這避免了直接調用可能會產生鏈接問題的外部函式
        state.homeKitStabilized = true;
    }
}

void SystemManager::handleHomeKitPairingDetection(unsigned long currentTime) {
    if (currentTime - state.lastPairingCheck >= PAIRING_CHECK_INTERVAL) {
        state.lastPairingCheck = currentTime;
        
        uint32_t currentMemory = ESP.getFreeHeap();
        updatePairingDetection(currentMemory);
        
        // 記錄配對狀態變化
        if (homeKitPairingActive != state.wasPairing) {
            DEBUG_INFO_PRINT("[SystemManager] HomeKit配對狀態變化: %s (記憶體: %d bytes)\n", 
                             homeKitPairingActive ? "配對中" : "空閒", currentMemory);
            state.wasPairing = homeKitPairingActive;
        }
    }
}

void SystemManager::handleWebServerProcessing(unsigned long currentTime) {
    if (!homeKitPairingActive && monitoringEnabled && webServer) {
        uint32_t freeMemory = ESP.getFreeHeap();
        unsigned long handleInterval = calculateWebServerInterval(freeMemory);
        
        bool shouldSkip = shouldSkipWebServerProcessing();
        
        if (!shouldSkip && currentTime - state.lastWebServerHandle >= handleInterval) {
            webServer->handleClient();
            state.lastWebServerHandle = currentTime;
        }
    }
}

void SystemManager::handleConfigurationMode() {
    // 配置模式處理已移回main.cpp以避免依賴問題
    // 這個方法保留為空，或者可以添加配置模式的狀態管理
    DEBUG_VERBOSE_PRINT("[SystemManager] 配置模式處理交由main.cpp處理\n");
}

void SystemManager::handlePeriodicTasks(unsigned long currentTime) {
    if (currentTime - state.lastLoopTime >= SYSTEM_HEARTBEAT_INTERVAL) {
        printHeartbeatInfo(currentTime);
        state.lastLoopTime = currentTime;
    }
}

void SystemManager::printHeartbeatInfo(unsigned long currentTime) {
    String mode, wifiStatus, deviceStatus, ipAddress;
    getSystemStats(mode, wifiStatus, deviceStatus, ipAddress);
    
    DEBUG_INFO_PRINT("[SystemManager] 主循環運行中... 模式：%s，WiFi：%s，設備：%s，IP：%s\n", 
                     mode.c_str(), wifiStatus.c_str(), deviceStatus.c_str(), ipAddress.c_str());
    
    // HomeKit 模式的詳細狀態
    if (homeKitInitialized) {
        // 記憶體監控和分析
        static uint32_t minMemory = ESP.getFreeHeap();
        static uint32_t maxMemory = ESP.getFreeHeap();
        uint32_t currentMemory = ESP.getFreeHeap();
        
        if (currentMemory < minMemory) minMemory = currentMemory;
        if (currentMemory > maxMemory) maxMemory = currentMemory;
        
        DEBUG_INFO_PRINT("[SystemManager] HomeKit狀態 - WiFi: %d dBm, 記憶體: %d bytes (最小:%d, 最大:%d), WebServer: %s, 配對中: %s\n", 
                         WiFi.RSSI(), currentMemory, minMemory, maxMemory,
                         monitoringEnabled ? "啟用" : "停用",
                         homeKitPairingActive ? "是" : "否");
        
        // 記憶體警告
        if (currentMemory < MEMORY_MEDIUM_THRESHOLD) {
            DEBUG_ERROR_PRINT("[SystemManager] ⚠️ 記憶體不足警告: %d bytes\n", currentMemory);
        }
    }
}

// === 輔助方法實現 ===

bool SystemManager::shouldStartWebServer(unsigned long currentTime) const {
    return state.webServerStartScheduled && 
           !monitoringEnabled && 
           currentTime - state.homeKitReadyTime >= WEBSERVER_STARTUP_DELAY && 
           !homeKitPairingActive;
}

unsigned long SystemManager::calculateWebServerInterval(uint32_t freeMemory) {
    if (freeMemory < MEMORY_TIGHT_THRESHOLD) {
        state.webServerSkipCounter++;
        return 200; // 記憶體緊張時降低頻率
    } else if (freeMemory < MEMORY_MEDIUM_THRESHOLD) {
        return 100; // 中等記憶體時中等頻率
    } else {
        state.webServerSkipCounter = 0;
        return 50;  // 記憶體充足時正常頻率
    }
}

bool SystemManager::shouldSkipWebServerProcessing() const {
    // 記憶體嚴重不足時偶爾跳過WebServer處理
    return (state.webServerSkipCounter > 10 && (state.webServerSkipCounter % 3) != 0);
}

void SystemManager::updatePairingDetection(uint32_t currentMemory) {
    // 更寬鬆的記憶體變化檢測，避免誤判
    state.avgMemory = (state.avgMemory * 3 + currentMemory) / 4; // 移動平均
    
    bool significantMemoryDrop = currentMemory < state.avgMemory - MEMORY_DROP_THRESHOLD;
    
    if (significantMemoryDrop) {
        state.pairingDetectionCounter++;
    } else {
        state.pairingDetectionCounter = 0;
    }
    
    // 連續檢測到記憶體下降才認為在配對
    homeKitPairingActive = (state.pairingDetectionCounter >= 2);
}

void SystemManager::getSystemStats(String& mode, String& wifiStatus, String& deviceStatus, String& ipAddress) {
    if (homeKitInitialized) {
        mode = "HomeKit模式";
    } else if (wifiManager != nullptr) {
        mode = "WiFi配置模式";
    } else {
        mode = "初始化中";
    }
    
    wifiStatus = (WiFi.status() == WL_CONNECTED) ? "已連接" : "未連接";
    deviceStatus = deviceInitialized ? "已初始化" : "未初始化";
    ipAddress = WiFi.localIP().toString();
}

void SystemManager::resetState() {
    state = SystemState();
    state.avgMemory = ESP.getFreeHeap();
    DEBUG_INFO_PRINT("[SystemManager] 系統狀態已重置\n");
}

bool SystemManager::shouldStartMonitoring() const {
    return state.homeKitStabilized && !monitoringEnabled;
}