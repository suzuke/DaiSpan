#include "common/SystemManager.h"
#include "common/Config.h"
#include "controller/IThermostatControl.h"
#include "device/ThermostatDevice.h"
#include "common/Debug.h"
#include "HomeSpan.h"
#ifdef ENABLE_OTA_UPDATE
#include <ArduinoOTA.h>
#endif

// 前向宣告避免包含問題的頭文件
class WiFiManager;

// 系統常數
static constexpr unsigned long POWER_CHECK_INTERVAL = 30000;     // ESP32-C3 功率檢查間隔
static constexpr unsigned long PAIRING_CHECK_INTERVAL = 3000;     // 配對檢測間隔
static constexpr unsigned long SYSTEM_HEARTBEAT_INTERVAL = 30000; // 系統心跳間隔
static constexpr unsigned long WEBSERVER_STARTUP_DELAY = 5000;    // WebServer 啟動延遲

// 記憶體閾值
static constexpr uint32_t MEMORY_TIGHT_THRESHOLD = 40000;   // 記憶體緊張（降載低優先任務）
static constexpr uint32_t MEMORY_CRITICAL_THRESHOLD = 30000; // 記憶體嚴重不足

SystemManager::SystemManager(ConfigManager& config, WiFiManager*& wifi, WebServer*& web,
                           IThermostatControl*& controller,
                           ThermostatDevice*& device, bool& devInit, bool& hkInit,
                           bool& monitoring, bool& pairing)
    : configManager(config), wifiManager(wifi), webServer(web),
      thermostatController(controller), thermostatDevice(device),
      deviceInitialized(devInit), homeKitInitialized(hkInit),
      monitoringEnabled(monitoring), homeKitPairingActive(pairing) {

    state.avgMemory = ESP.getFreeHeap();
    registerTasks();
    DEBUG_INFO_PRINT("[SystemManager] 初始化完成\n");
}

void SystemManager::registerTasks() {
    scheduler.addTask(DaiSpan::Core::TaskPriority::Medium, "S21Update", 2000, [this]() {
        if (thermostatController) {
            thermostatController->update();
        }
    });

    scheduler.addTask(DaiSpan::Core::TaskPriority::Medium, "PairingDetection", PAIRING_CHECK_INTERVAL, [this]() {
        handleHomeKitPairingDetection(millis());
    });

#ifdef ENABLE_OTA_UPDATE
    scheduler.addTask(DaiSpan::Core::TaskPriority::Medium, "OTAHandle", 100, []() {
        ArduinoOTA.handle();
    });
#endif

    scheduler.addTask(DaiSpan::Core::TaskPriority::Low, "WiFiMonitor", 5000, [this]() {
        handleGlobalWiFiMonitoring(millis());
    });

    scheduler.addTask(DaiSpan::Core::TaskPriority::Low, "WiFiPower", POWER_CHECK_INTERVAL, [this]() {
        handleSmartWiFiPowerManagement();
    });

    scheduler.addTask(DaiSpan::Core::TaskPriority::Low, "WebServer", 25, [this]() {
        if (homeKitInitialized && !homeKitPairingActive && monitoringEnabled && webServer) {
            webServer->handleClient();
        }
    });

    scheduler.addTask(DaiSpan::Core::TaskPriority::Low, "Heartbeat", SYSTEM_HEARTBEAT_INTERVAL, [this]() {
        printHeartbeatInfo(millis());
    });
}

void SystemManager::processMainLoop() {
    uint32_t now = millis();

    if (homeKitInitialized) {
        homeSpan.poll();
    }

    uint32_t freeHeap = ESP.getFreeHeap();
    state.avgMemory = (state.avgMemory * 7 + freeHeap) / 8;

    MemoryPressure pressure = MemoryPressure::Normal;
    if (freeHeap < MEMORY_CRITICAL_THRESHOLD) {
        pressure = MemoryPressure::Critical;
    } else if (freeHeap < MEMORY_TIGHT_THRESHOLD) {
        pressure = MemoryPressure::Tight;
    }
    state.memoryPressure = pressure;

    bool lowThrottle = pressure != MemoryPressure::Normal;
    scheduler.setLowPriorityThrottled(lowThrottle);

    uint32_t skipInterval = 200;
    if (pressure == MemoryPressure::Tight) {
        skipInterval = 400;
    } else if (pressure == MemoryPressure::Critical) {
        skipInterval = 700;
    }
    scheduler.setLowPrioritySkipInterval(skipInterval);

    scheduler.run(now);
    state.lastTaskCount = scheduler.getLastExecutedCount();
    state.lastSchedulerDurationUs = scheduler.getLastRunDurationUs();

    if (homeKitInitialized && !homeKitPairingActive) {
        if (state.homeKitReadyTime == 0) {
            state.homeKitReadyTime = now;
        }
        if (!state.homeKitStabilized && (now - state.homeKitReadyTime) >= WEBSERVER_STARTUP_DELAY) {
            state.homeKitStabilized = true;
        }
    } else {
        state.homeKitReadyTime = now;
    }
}

void SystemManager::handleGlobalWiFiMonitoring(unsigned long currentTime) {
    static unsigned long lastWiFiReconnectAttempt = 0;
    static int wifiFailureCount = 0;

    wl_status_t wifiStatus = WiFi.status();

    if (wifiStatus != WL_CONNECTED) {
        wifiFailureCount++;
        DEBUG_WARN_PRINT("[SystemManager] WiFi斷線檢測 (狀態: %d, 失敗計數: %d)\n", wifiStatus, wifiFailureCount);

        if (currentTime - lastWiFiReconnectAttempt >= 10000) {
            lastWiFiReconnectAttempt = currentTime;

            String ssid = configManager.getWiFiSSID();
            String password = configManager.getWiFiPassword();

            if (ssid.length() > 0 && ssid != "UNCONFIGURED_SSID") {
                DEBUG_INFO_PRINT("[SystemManager] 全局WiFi重連嘗試 - SSID: %s\n", ssid.c_str());

                WiFi.disconnect(false);
                delay(100);
                WiFi.begin(ssid.c_str(), password.c_str());

                unsigned long connectStart = millis();
                while (WiFi.status() != WL_CONNECTED && millis() - connectStart < 10000) {
                    delay(100);
                    if (homeKitInitialized) {
                        homeSpan.poll();
                    }
                }

                if (WiFi.status() == WL_CONNECTED) {
                    wifiFailureCount = 0;
                    DEBUG_INFO_PRINT("[SystemManager] WiFi重連成功 - IP: %s\n", WiFi.localIP().toString().c_str());
                } else {
                    DEBUG_ERROR_PRINT("[SystemManager] WiFi重連失敗 (嘗試 %d 次)\n", wifiFailureCount);
                }
            }
        }
    } else if (wifiFailureCount > 0) {
        DEBUG_INFO_PRINT("[SystemManager] WiFi連接恢復正常\n");
        wifiFailureCount = 0;
    }
}

void SystemManager::handleSmartWiFiPowerManagement() {
    wl_status_t wifiStatus = WiFi.status();

    if (wifiStatus == WL_CONNECTED) {
        int32_t rssi = WiFi.RSSI();
        wifi_power_t currentPower = WiFi.getTxPower();
        wifi_power_t targetPower = WIFI_POWER_11dBm;

        if (rssi < -75) {
            targetPower = WIFI_POWER_15dBm;
        } else if (rssi > -50) {
            targetPower = WIFI_POWER_8_5dBm;
        }

        if (currentPower != targetPower) {
            WiFi.setTxPower(targetPower);
            DEBUG_INFO_PRINT("[SystemManager] 智能調整WiFi功率: %d dBm (RSSI: %d dBm)\n",
                             (int)targetPower, rssi);
        }
    } else if (WiFi.getTxPower() < WIFI_POWER_11dBm) {
        WiFi.setTxPower(WIFI_POWER_11dBm);
        DEBUG_INFO_PRINT("[SystemManager] 斷線時提升WiFi功率至11dBm以增強重連能力\n");
    }
}

void SystemManager::handleHomeKitPairingDetection(unsigned long currentTime) {
    uint32_t currentMemory = ESP.getFreeHeap();
    updatePairingDetection(currentMemory);

    if (homeKitPairingActive != state.wasPairing) {
        DEBUG_INFO_PRINT("[SystemManager] HomeKit配對狀態變化: %s (記憶體: %d bytes)\n",
                         homeKitPairingActive ? "配對中" : "空閒", currentMemory);
        state.wasPairing = homeKitPairingActive;
    }
}

void SystemManager::printHeartbeatInfo(unsigned long currentTime) {
    String mode, wifiStatus, deviceStatus, ipAddress;
    getSystemStats(mode, wifiStatus, deviceStatus, ipAddress);

    DEBUG_INFO_PRINT("[SystemManager] 主循環運行中... 模式：%s，WiFi：%s，設備：%s，IP：%s\n",
                     mode.c_str(), wifiStatus.c_str(), deviceStatus.c_str(), ipAddress.c_str());

    String pressureText = getMemoryPressureText();
    DEBUG_INFO_PRINT("[SystemManager] Scheduler狀態 - 任務:%u，耗時:%lu μs，記憶體壓力:%s\n",
                     state.lastTaskCount,
                     static_cast<unsigned long>(state.lastSchedulerDurationUs),
                     pressureText.c_str());

    if (homeKitInitialized) {
        static uint32_t minMemory = ESP.getFreeHeap();
        static uint32_t maxMemory = ESP.getFreeHeap();
        uint32_t currentMemory = ESP.getFreeHeap();

        if (currentMemory < minMemory) minMemory = currentMemory;
        if (currentMemory > maxMemory) maxMemory = currentMemory;

        DEBUG_INFO_PRINT("[SystemManager] HomeKit狀態 - WiFi: %d dBm, 記憶體: %d bytes (最小:%d, 最大:%d), WebServer: %s, 配對中: %s\n",
                         WiFi.RSSI(), currentMemory, minMemory, maxMemory,
                         monitoringEnabled ? "啟用" : "停用",
                         homeKitPairingActive ? "是" : "否");

        if (currentMemory < MEMORY_TIGHT_THRESHOLD) {
            DEBUG_WARN_PRINT("[SystemManager] ⚠️ 記憶體偏低: %d bytes\n", currentMemory);
        }
    }
}

void SystemManager::updatePairingDetection(uint32_t currentMemory) {
    if (state.avgMemory == 0) {
        state.avgMemory = currentMemory;
    }
    state.avgMemory = (state.avgMemory * 7 + currentMemory) / 8;

    static unsigned long lastPairingCheckTime = 0;
    static uint32_t lastMemoryReading = currentMemory;
    static int stableMemoryCount = 0;

    unsigned long currentTime = millis();

    if (currentTime - lastPairingCheckTime > 5000) {
        lastPairingCheckTime = currentTime;

        if (abs((int)currentMemory - (int)lastMemoryReading) < 2000) {
            stableMemoryCount++;
        } else {
            stableMemoryCount = 0;
        }

        if (stableMemoryCount >= 3 && currentMemory > 15000) {
            if (homeKitPairingActive) {
                homeKitPairingActive = false;
                DEBUG_INFO_PRINT("[SystemManager] HomeKit配對活動結束（記憶體穩定: %d bytes）\n",
                                 currentMemory);
            }
        } else if (currentMemory < 20000 && (state.avgMemory > currentMemory + 10000)) {
            if (!homeKitPairingActive) {
                homeKitPairingActive = true;
                DEBUG_INFO_PRINT("[SystemManager] 檢測到HomeKit配對活動（記憶體急劇下降）\n");
            }
        }

        lastMemoryReading = currentMemory;
    }

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

String SystemManager::getMemoryPressureText() const {
    switch (state.memoryPressure) {
        case MemoryPressure::Tight:
            return F("緊張");
        case MemoryPressure::Critical:
            return F("嚴重");
        case MemoryPressure::Normal:
        default:
            return F("正常");
    }
}

uint8_t SystemManager::getMemoryPressureLevel() const {
    switch (state.memoryPressure) {
        case MemoryPressure::Tight:
            return 1;
        case MemoryPressure::Critical:
            return 2;
        default:
            return 0;
    }
}

void SystemManager::resetState() {
    state = RuntimeState();
    state.avgMemory = ESP.getFreeHeap();
    DEBUG_INFO_PRINT("[SystemManager] 系統狀態已重置\n");
}

bool SystemManager::shouldStartMonitoring() const {
    return state.homeKitStabilized && !monitoringEnabled;
}
