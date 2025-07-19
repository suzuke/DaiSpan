#pragma once

#include <WiFi.h>
#include <functional>
#include <vector>
#include <queue>
#include <memory>
#include "Debug.h"
#include "architecture_v3/core/EventSystemSimple.h"
#include "architecture_v3/core/Result.h"

namespace DaiSpan::WiFi {

/**
 * WiFi網路信息結構
 */
struct NetworkInfo {
    String ssid;
    int32_t rssi;
    wifi_auth_mode_t encryption;
    uint8_t channel;
    bool hidden;
    uint8_t* bssid;
    
    NetworkInfo() : rssi(0), encryption(WIFI_AUTH_OPEN), channel(0), hidden(false), bssid(nullptr) {}
    
    NetworkInfo(const String& _ssid, int32_t _rssi, wifi_auth_mode_t _encryption, 
                uint8_t _channel, bool _hidden = false)
        : ssid(_ssid), rssi(_rssi), encryption(_encryption), channel(_channel), hidden(_hidden), bssid(nullptr) {}
    
    // 獲取加密類型字符串
    String getEncryptionString() const {
        switch (encryption) {
            case WIFI_AUTH_OPEN: return "開放";
            case WIFI_AUTH_WEP: return "WEP";
            case WIFI_AUTH_WPA_PSK: return "WPA";
            case WIFI_AUTH_WPA2_PSK: return "WPA2";
            case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
            case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2企業";
            case WIFI_AUTH_WPA3_PSK: return "WPA3";
            default: return "未知";
        }
    }
    
    // 獲取信號強度等級
    int getSignalStrength() const {
        if (rssi >= -50) return 4; // 優秀
        if (rssi >= -60) return 3; // 良好
        if (rssi >= -70) return 2; // 一般
        if (rssi >= -80) return 1; // 較差
        return 0; // 極差
    }
    
    // 判斷是否為安全網路
    bool isSecure() const {
        return encryption != WIFI_AUTH_OPEN;
    }
};

/**
 * WiFi掃描事件
 */
class WiFiScanEvent : public Core::DomainEvent {
public:
    static constexpr const char* EVENT_TYPE_NAME = "WiFiScanComplete";
    std::string getEventType() const override { return EVENT_TYPE_NAME; }
    
    std::vector<NetworkInfo> networks;
    uint32_t requestId;
    bool success;
    String errorMessage;
    
    WiFiScanEvent(uint32_t _requestId, const std::vector<NetworkInfo>& _networks)
        : requestId(_requestId), networks(_networks), success(true) {}
    
    WiFiScanEvent(uint32_t _requestId, const String& _errorMessage)
        : requestId(_requestId), success(false), errorMessage(_errorMessage) {}
};

/**
 * 異步WiFi掃描器
 * 實現非阻塞WiFi網路掃描，避免主循環凍結
 */
class AsyncWiFiScanner {
public:
    using ScanCallback = std::function<void(const std::vector<NetworkInfo>&)>;
    using ErrorCallback = std::function<void(const String&)>;
    
    enum class ScanState {
        IDLE,           // 空閒狀態
        SCANNING,       // 掃描中
        PROCESSING,     // 處理結果
        COMPLETE,       // 完成
        ERROR          // 錯誤
    };
    
    struct ScanRequest {
        uint32_t requestId;
        ScanCallback onComplete;
        ErrorCallback onError;
        unsigned long requestTime;
        unsigned long timeout;
        
        ScanRequest(uint32_t _id, ScanCallback _onComplete, ErrorCallback _onError, 
                   unsigned long _timeout = 10000)
            : requestId(_id), onComplete(_onComplete), onError(_onError), 
              requestTime(millis()), timeout(_timeout) {}
    };

private:
    static constexpr unsigned long DEFAULT_SCAN_TIMEOUT = 10000; // 10秒默認超時
    static constexpr unsigned long CACHE_DURATION = 30000;      // 30秒緩存有效期
    static constexpr unsigned long MIN_SCAN_INTERVAL = 2000;    // 最小掃描間隔2秒
    static constexpr size_t MAX_PENDING_REQUESTS = 5;           // 最大待處理請求數
    
    ScanState currentState;
    std::queue<ScanRequest> pendingRequests;
    std::unique_ptr<ScanRequest> currentRequest;
    
    // 緩存系統
    std::vector<NetworkInfo> cachedNetworks;
    unsigned long lastScanTime;
    unsigned long scanStartTime;
    
    // 狀態管理
    uint32_t nextRequestId;
    bool wifiEventHandlerRegistered;
    
    // 事件系統集成
    Core::EventPublisher* eventBus;
    
    // 統計信息
    struct Statistics {
        uint32_t totalScans;
        uint32_t successfulScans;
        uint32_t failedScans;
        uint32_t timeouts;
        uint32_t cacheHits;
        unsigned long totalScanTime;
        unsigned long averageScanTime;
        
        Statistics() : totalScans(0), successfulScans(0), failedScans(0), 
                      timeouts(0), cacheHits(0), totalScanTime(0), averageScanTime(0) {}
    } stats;

public:
    explicit AsyncWiFiScanner(Core::EventPublisher* _eventBus = nullptr);
    ~AsyncWiFiScanner();
    
    /**
     * 開始異步WiFi掃描
     * @param onComplete 掃描完成回調
     * @param onError 錯誤回調
     * @param timeout 超時時間（毫秒）
     * @return 請求ID，用於取消請求
     */
    uint32_t startScan(ScanCallback onComplete, ErrorCallback onError = nullptr, 
                      unsigned long timeout = DEFAULT_SCAN_TIMEOUT);
    
    /**
     * 取消待處理的掃描請求
     * @param requestId 請求ID
     * @return 是否成功取消
     */
    bool cancelRequest(uint32_t requestId);
    
    /**
     * 取消所有待處理的請求
     */
    void cancelAllRequests();
    
    /**
     * 主循環更新函數（需要在主循環中調用）
     */
    void loop();
    
    /**
     * 獲取當前掃描狀態
     */
    ScanState getCurrentState() const { return currentState; }
    
    /**
     * 獲取當前掃描狀態字符串
     */
    String getCurrentStateString() const;
    
    /**
     * 獲取緩存的網路列表
     */
    const std::vector<NetworkInfo>& getCachedNetworks() const { return cachedNetworks; }
    
    /**
     * 檢查緩存是否有效
     */
    bool isCacheValid() const {
        return (millis() - lastScanTime) < CACHE_DURATION && !cachedNetworks.empty();
    }
    
    /**
     * 強制刷新緩存
     */
    void invalidateCache() {
        cachedNetworks.clear();
        lastScanTime = 0;
    }
    
    /**
     * 獲取待處理請求數量
     */
    size_t getPendingRequestCount() const { return pendingRequests.size(); }
    
    /**
     * 獲取統計信息
     */
    const Statistics& getStatistics() const { return stats; }
    
    /**
     * 重置統計信息
     */
    void resetStatistics() { stats = Statistics(); }
    
    /**
     * 設置事件發布器
     */
    void setEventBus(Core::EventPublisher* _eventBus) { eventBus = _eventBus; }

    /**
     * WiFi事件處理器（公有，供靜態函數調用）
     */
    void handleWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);

private:
    
    /**
     * 註冊WiFi事件處理器
     */
    void registerWiFiEventHandler();
    
    /**
     * 取消註冊WiFi事件處理器
     */
    void unregisterWiFiEventHandler();
    
    /**
     * 開始下一個掃描請求
     */
    void startNextScan();
    
    /**
     * 處理掃描完成
     */
    void handleScanComplete();
    
    /**
     * 處理掃描錯誤
     */
    void handleScanError(const String& errorMessage);
    
    /**
     * 處理掃描超時
     */
    void handleScanTimeout();
    
    /**
     * 解析掃描結果
     */
    std::vector<NetworkInfo> parseScanResults();
    
    /**
     * 更新統計信息
     */
    void updateStatistics(bool success, unsigned long scanDuration);
    
    /**
     * 生成唯一請求ID
     */
    uint32_t generateRequestId() { return ++nextRequestId; }
    
    /**
     * 驗證WiFi掃描環境
     */
    Core::Result<void> validateScanEnvironment();
    
    /**
     * 清理當前請求
     */
    void cleanupCurrentRequest();
    
    /**
     * 發布事件到事件系統
     */
    void publishEvent(std::unique_ptr<Core::DomainEvent> event);
};

} // namespace DaiSpan::WiFi