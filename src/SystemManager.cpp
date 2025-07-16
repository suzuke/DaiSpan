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
static constexpr unsigned long PAIRING_CHECK_INTERVAL = 3000;    // 配對檢測間隔（優化：從5秒縮短）
static constexpr unsigned long WEBSERVER_STARTUP_DELAY = 5000;   // WebServer 啟動延遲
static constexpr unsigned long SYSTEM_HEARTBEAT_INTERVAL = 30000; // 系統心跳間隔

// 記憶體閾值 - 優化後減少偽休眠問題
static constexpr uint32_t MEMORY_DROP_THRESHOLD = 35000;         // 記憶體下降閾值（提高避免誤判）
static constexpr uint32_t MEMORY_TIGHT_THRESHOLD = 40000;        // 記憶體緊張閾值（降低減少觸發）
static constexpr uint32_t MEMORY_MEDIUM_THRESHOLD = 70000;       // 記憶體中等閾值（調整平衡點）

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
    // 高性能循環計數器系統 - 減少millis()調用
    state.loopCounter++;
    bool shouldCheckTiming = (state.loopCounter % state.fastLoopDivider) == 0;
    
    // 只在必要時獲取當前時間
    unsigned long currentTime = shouldCheckTiming ? millis() : 0;
    
    // 關鍵系統處理 - 每次循環都執行
    if (homeKitInitialized) {
        homeSpan.poll(); // 最高優先級
    }
    
    // 中等優先級處理 - 每10次循環檢查一次
    if ((state.loopCounter % 10) == 0) {
        handleOTAUpdates();
    }
    
    // 定時任務處理 - 使用優化的定時系統
    if (shouldCheckTiming) {
        handleOptimizedTimingTasks(currentTime);
    }
}

void SystemManager::handleOptimizedTimingTasks(unsigned long currentTime) {
    // 高性能統一定時檢查 - 一次檢查所有定時器
    
    // 全局WiFi監控 (最高優先級 - 快速重連)
    if (currentTime >= state.nextWiFiCheck) {
        state.nextWiFiCheck = currentTime + 5000; // 5秒檢查間隔（優化：從15秒縮短）
        handleGlobalWiFiMonitoring(currentTime);
    }
    
    // WiFi 智能功率管理檢查
    if (currentTime >= state.nextPowerCheck) {
        state.nextPowerCheck = currentTime + POWER_CHECK_INTERVAL;
        #if defined(ESP32C3_SUPER_MINI)
        handleSmartWiFiPowerManagement();
        #endif
    }
    
    // HomeKit 配對檢測
    if (homeKitInitialized && currentTime >= state.nextPairingCheck) {
        state.nextPairingCheck = currentTime + PAIRING_CHECK_INTERVAL;
        handleHomeKitPairingDetection(currentTime);
    }
    
    // WebServer 處理
    if (homeKitInitialized && !homeKitPairingActive && monitoringEnabled && webServer) {
        uint32_t freeMemory = ESP.getFreeHeap();
        unsigned long handleInterval = calculateWebServerInterval(freeMemory);
        
        if (currentTime >= state.nextWebServerHandle) {
            state.nextWebServerHandle = currentTime + handleInterval;
            if (!shouldSkipWebServerProcessing()) {
                webServer->handleClient();
            }
        }
    }
    
    // WebServer 啟動檢查
    if (homeKitInitialized) {
        handleWebServerStartup(currentTime);
    }
    
    // 系統心跳
    if (currentTime >= state.nextHeartbeat) {
        state.nextHeartbeat = currentTime + SYSTEM_HEARTBEAT_INTERVAL;
        printHeartbeatInfo(currentTime);
    }
}

void SystemManager::handleGlobalWiFiMonitoring(unsigned long currentTime) {
    // 全局WiFi監控 - 即使在HomeKit模式下也要確保WiFi連接
    static unsigned long lastWiFiReconnectAttempt = 0;
    static int wifiFailureCount = 0;
    
    wl_status_t wifiStatus = WiFi.status();
    
    if (wifiStatus != WL_CONNECTED) {
        wifiFailureCount++;
        DEBUG_WARN_PRINT("[SystemManager] WiFi斷線檢測 (狀態: %d, 失敗計數: %d)\n", wifiStatus, wifiFailureCount);
        
        // 快速重連策略：10秒間隔，快速恢復連接（優化：從30秒縮短）
        if (currentTime - lastWiFiReconnectAttempt >= 10000) { // 10秒重連間隔
            lastWiFiReconnectAttempt = currentTime;
            
            // 獲取WiFi憑證
            String ssid = configManager.getWiFiSSID();
            String password = configManager.getWiFiPassword();
            
            if (ssid.length() > 0 && ssid != "UNCONFIGURED_SSID") {
                DEBUG_INFO_PRINT("[SystemManager] 全局WiFi重連嘗試 - SSID: %s\n", ssid.c_str());
                
                // 溫和的重連策略
                WiFi.disconnect(false);
                delay(100);
                WiFi.begin(ssid.c_str(), password.c_str());
                
                // 非阻塞等待連接建立
                unsigned long connectStart = millis();
                while (WiFi.status() != WL_CONNECTED && millis() - connectStart < 10000) {
                    delay(100);
                    // 允許其他任務繼續執行
                    if (homeKitInitialized) {
                        homeSpan.poll();
                    }
                }
                
                if (WiFi.status() == WL_CONNECTED) {
                    wifiFailureCount = 0; // 重置失敗計數
                    DEBUG_INFO_PRINT("[SystemManager] WiFi重連成功 - IP: %s\n", WiFi.localIP().toString().c_str());
                } else {
                    DEBUG_ERROR_PRINT("[SystemManager] WiFi重連失敗 (嘗試 %d 次)\n", wifiFailureCount);
                }
            }
        }
    } else {
        // WiFi已連接，重置失敗計數
        if (wifiFailureCount > 0) {
            DEBUG_INFO_PRINT("[SystemManager] WiFi連接恢復正常\n");
            wifiFailureCount = 0;
        }
    }
}

void SystemManager::handleESP32C3PowerManagement(unsigned long currentTime) {
    // 方法保留用於向後兼容，實際邏輯已移至 handleOptimizedTimingTasks
}

void SystemManager::handleOTAUpdates() {
    if (WiFi.status() == WL_CONNECTED) {
        ArduinoOTA.handle();
    }
}

void SystemManager::handleSmartWiFiPowerManagement() {
    // 智能WiFi功率管理 - 根據連接狀態和信號強度動態調整
    wl_status_t wifiStatus = WiFi.status();
    
    if (wifiStatus == WL_CONNECTED) {
        // 連接正常時，根據信號強度調整功率
        int32_t rssi = WiFi.RSSI();
        wifi_power_t currentPower = WiFi.getTxPower();
        wifi_power_t targetPower = WIFI_POWER_11dBm; // 預設中等功率
        
        if (rssi < -75) {
            // 信號弱，使用較高功率
            targetPower = WIFI_POWER_15dBm;
        } else if (rssi > -50) {
            // 信號很強，可以使用較低功率
            targetPower = WIFI_POWER_8_5dBm;
        }
        
        if (currentPower != targetPower) {
            WiFi.setTxPower(targetPower);
            DEBUG_INFO_PRINT("[SystemManager] 智能調整WiFi功率: %d dBm (RSSI: %d dBm)\n", 
                            (int)targetPower, rssi);
        }
    } else {
        // 連接斷開時，使用較高功率以確保能夠重連
        if (WiFi.getTxPower() < WIFI_POWER_11dBm) {
            WiFi.setTxPower(WIFI_POWER_11dBm);
            DEBUG_INFO_PRINT("[SystemManager] 斷線時提升WiFi功率至11dBm以增強重連能力\n");
        }
    }
}

void SystemManager::handleHomeKitMode(unsigned long currentTime) {
    // 此方法已由 handleOptimizedTimingTasks 替代，保留用於向後兼容
    handleOptimizedTimingTasks(currentTime);
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
    uint32_t currentMemory = ESP.getFreeHeap();
    updatePairingDetection(currentMemory);
    
    // 記錄配對狀態變化
    if (homeKitPairingActive != state.wasPairing) {
        DEBUG_INFO_PRINT("[SystemManager] HomeKit配對狀態變化: %s (記憶體: %d bytes)\n", 
                         homeKitPairingActive ? "配對中" : "空閒", currentMemory);
        state.wasPairing = homeKitPairingActive;
    }
}

void SystemManager::handleWebServerProcessing(unsigned long currentTime) {
    // 此方法已由 handleOptimizedTimingTasks 替代，保留用於向後兼容
}

void SystemManager::handleConfigurationMode() {
    // 配置模式處理已移回main.cpp以避免依賴問題
    // 這個方法保留為空，或者可以添加配置模式的狀態管理
    DEBUG_VERBOSE_PRINT("[SystemManager] 配置模式處理交由main.cpp處理\n");
}

void SystemManager::handlePeriodicTasks(unsigned long currentTime) {
    // 此方法已由 handleOptimizedTimingTasks 替代，保留用於向後兼容
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
    // 優化WebServer跳過邏輯 - 減少跳過頻率，提高響應性
    // 只有在真正記憶體不足時才跳過，且跳過比例從2/3降低為1/2
    if (state.webServerSkipCounter <= 5) {
        return false; // 初期不跳過，確保基本響應
    }
    
    // 記憶體嚴重不足時，每2次跳過1次（改善前是每3次跳過2次）
    return (state.webServerSkipCounter % 2) != 0;
}

void SystemManager::updatePairingDetection(uint32_t currentMemory) {
    // 高性能記憶體檢測，使用移動平均減少波動影響
    state.avgMemory = (state.avgMemory * 7 + currentMemory) / 8; // 更穩定的移動平均
    
    // 改良的配對檢測邏輯 - 使用 HomeSpan 實際連接狀態
    static unsigned long lastPairingCheckTime = 0;
    static uint32_t lastMemoryReading = currentMemory;
    static int stableMemoryCount = 0;
    
    unsigned long currentTime = millis();
    
    // 每5秒檢查一次配對狀態
    if (currentTime - lastPairingCheckTime > 5000) {
        lastPairingCheckTime = currentTime;
        
        // 檢查記憶體是否穩定（5次檢查內變化小於2KB）
        if (abs((int)currentMemory - (int)lastMemoryReading) < 2000) {
            stableMemoryCount++;
        } else {
            stableMemoryCount = 0;
        }
        
        // 如果記憶體穩定且不是極低，則認為配對已完成
        if (stableMemoryCount >= 3 && currentMemory > 15000) {
            if (homeKitPairingActive) {
                homeKitPairingActive = false;
                DEBUG_INFO_PRINT("[SystemManager] HomeKit配對活動結束（記憶體穩定: %d bytes，穩定計數: %d）\n", 
                                currentMemory, stableMemoryCount);
            }
        }
        // 只有在記憶體急劇下降且系統處於不穩定狀態時才認為是配對中
        else if (currentMemory < 20000 && (state.avgMemory - currentMemory) > 10000) {
            if (!homeKitPairingActive) {
                homeKitPairingActive = true;
                DEBUG_INFO_PRINT("[SystemManager] 檢測到HomeKit配對活動（記憶體急劇下降: %d bytes < %d bytes）\n", 
                                currentMemory, (int)(state.avgMemory - 10000));
            }
        }
        
        lastMemoryReading = currentMemory;
    }
    
    // 強制清除配對狀態：如果記憶體極低但穩定，說明不是配對導致的
    if (currentMemory < 20000 && stableMemoryCount >= 5) {
        if (homeKitPairingActive) {
            homeKitPairingActive = false;
            DEBUG_INFO_PRINT("[SystemManager] 強制清除配對狀態（記憶體低但穩定: %d bytes）\n", currentMemory);
        }
    }
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
    state = OptimizedTimingSystem();
    state.avgMemory = ESP.getFreeHeap();
    DEBUG_INFO_PRINT("[SystemManager] 系統狀態已重置\n");
}

bool SystemManager::shouldStartMonitoring() const {
    return state.homeKitStabilized && !monitoringEnabled;
}