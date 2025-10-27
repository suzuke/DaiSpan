#pragma once

#include "WiFi.h"
#include "WebServer.h"
#include "DNSServer.h"
#include "Update.h"
#include "Config.h"
#include "Debug.h"
#include "LogManager.h"
#include "WebUI.h"
#include "esp_wifi.h"

// 前向聲明
extern void safeRestart();

class WiFiManager {
private:
    ConfigManager& config;
    WebServer* webServer;
    DNSServer* dnsServer;
    bool isAPMode;
    bool isConfigured;
    int connectionAttempts;
    unsigned long lastConnectionAttempt;
    String webAuthUser;
    String webAuthPassword;
    
    // AP 模式配置
    static constexpr const char* AP_SSID = "DaiSpan-Config";
    static constexpr const char* AP_PASSWORD = nullptr;
    static constexpr int MAX_CONNECTION_ATTEMPTS = 3;
    static constexpr int CONNECTION_TIMEOUT = 15000; // 15 秒 (增加超時時間)
    
    // 優化WiFi重連管理 - 快速故障恢復
    static constexpr unsigned long WIFI_RECONNECT_INTERVAL = 30000;  // 30秒 (快速重連)
    static constexpr unsigned long WIFI_STABILITY_CHECK_INTERVAL = 15000; // 15秒檢查一次
    static constexpr int WIFI_SIGNAL_THRESHOLD = -75; // 提高RSSI閾值
    unsigned long lastWiFiStabilityCheck;
    int consecutiveFailures;
    bool wifiStabilityMode; // 穩定模式：減少不必要的重連
    
    // 網路掃描優化
    unsigned long lastNetworkScan;
    static constexpr unsigned long NETWORK_SCAN_CACHE_TIME = 30000; // 30秒掃描緩存
    
public:
    WiFiManager(ConfigManager& cfg) : 
        config(cfg), 
        webServer(nullptr), 
        dnsServer(nullptr),
        isAPMode(false), 
        isConfigured(false),
        connectionAttempts(0),
        lastConnectionAttempt(0),
        lastWiFiStabilityCheck(0),
        consecutiveFailures(0),
        wifiStabilityMode(false),
        lastNetworkScan(0) {
        // 延遲到 WiFi 初始化後再生成認證憑證
    }
    
    ~WiFiManager() {
        if (webServer) delete webServer;
        if (dnsServer) delete dnsServer;
    }
    
    // 初始化 WiFi 管理器
    bool begin() {
        DEBUG_INFO_PRINT("[WiFiManager] 開始初始化...\n");
        
        // 檢查是否已有配置
        String ssid = config.getWiFiSSID();
        String password = config.getWiFiPassword();
        
        // 檢查是否為強制 AP 模式或無效配置
        bool isConfigured = config.isWiFiConfigured();
        
        DEBUG_INFO_PRINT("[WiFiManager] WiFi 配置狀態: %s, SSID: %s\n", 
                         isConfigured ? "已配置" : "未配置", ssid.c_str());
        
        // 如果未配置或是默認配置，則啟動 AP 模式
        if (!isConfigured || ssid == "YourWiFiSSID" || ssid.length() == 0) {
            DEBUG_INFO_PRINT("[WiFiManager] 未找到有效的 WiFi 配置，啟動 AP 模式\n");
            return startAPMode();
        }
        
        // 嘗試連接已配置的 WiFi（實現重試邏輯）
        connectionAttempts = 0; // 重置計數器
        
        while (connectionAttempts < MAX_CONNECTION_ATTEMPTS) {
            if (connectToWiFi(ssid, password)) {
                this->isConfigured = true;
                return true;
            }
            
            // 如果還有重試次數，等待一段時間後再試
            if (connectionAttempts < MAX_CONNECTION_ATTEMPTS) {
                DEBUG_INFO_PRINT("[WiFiManager] 等待 2 秒後重試...\n");
                delay(2000);
            }
        }
        
        // 所有重試都失敗，啟動 AP 模式
        DEBUG_ERROR_PRINT("[WiFiManager] WiFi 連接失敗 (%d/%d 次嘗試)，啟動 AP 模式\n", 
                         connectionAttempts, MAX_CONNECTION_ATTEMPTS);
        return startAPMode();
    }
    
    // 嘗試連接 WiFi
    bool connectToWiFi(const String& ssid, const String& password) {
        DEBUG_INFO_PRINT("[WiFiManager] 嘗試連接 WiFi: %s\n", ssid.c_str());
        
        // 輸入驗證
        if (ssid.length() == 0) {
            DEBUG_ERROR_PRINT("[WiFiManager] SSID 為空，無法連接\n");
            return false;
        }
        
        if (ssid.length() > 32) {
            DEBUG_ERROR_PRINT("[WiFiManager] SSID 過長：%d 字符\n", ssid.length());
            return false;
        }
        
        if (password.length() > 63) {
            DEBUG_ERROR_PRINT("[WiFiManager] 密碼過長：%d 字符\n", password.length());
            return false;
        }
        
        DEBUG_INFO_PRINT("[WiFiManager] 連接參數 - SSID: '%s', 密碼長度: %d\n", 
                         ssid.c_str(), password.length());
        
        // 溫和的模式切換 - 減少對系統的衝擊
        if (isAPMode) {
            DEBUG_INFO_PRINT("[WiFiManager] 溫和切換：從AP模式切換到STA模式\n");
            
            // 先停止服務，但保持WiFi模組活躍
            if (dnsServer) {
                dnsServer->stop();
                delete dnsServer;
                dnsServer = nullptr;
            }
            
            // 保留webServer，避免重複創建銷毀
            // 停止 SoftAP，但使用溫和的方式
            WiFi.softAPdisconnect(false); // false = 不強制斷開所有連接
            delay(200); // 增加延遲確保平穩切換
            
            isAPMode = false;
        }
        
        // 更溫和的WiFi狀態重置
        if (WiFi.getMode() != WIFI_STA) {
            WiFi.mode(WIFI_STA);
            delay(300); // 增加延遲確保模式切換完成
        }
        
        // 只在真正需要時才斷開連接
        if (WiFi.status() == WL_CONNECTED) {
            WiFi.disconnect(false); // 溫和斷開，不清除配置
            delay(100);
        }
        
        // 驗證模式切換成功
        wifi_mode_t currentMode = WiFi.getMode();
        if (currentMode != WIFI_STA) {
            DEBUG_WARN_PRINT("[WiFiManager] WiFi 模式切換失敗，當前模式: %d\n", currentMode);
        }
        
        DEBUG_INFO_PRINT("[WiFiManager] 開始連接 WiFi...\n");
        
        // 先檢查網路是否可見
        int n = WiFi.scanNetworks();
        bool networkFound = false;
        int networkRSSI = 0;
        String networkEncryption = "";
        
        for (int i = 0; i < n; i++) {
            if (WiFi.SSID(i) == ssid) {
                networkFound = true;
                networkRSSI = WiFi.RSSI(i);
                wifi_auth_mode_t encType = WiFi.encryptionType(i);
                networkEncryption = (encType == WIFI_AUTH_OPEN) ? "開放" : 
                                  (encType == WIFI_AUTH_WPA_PSK) ? "WPA" :
                                  (encType == WIFI_AUTH_WPA2_PSK) ? "WPA2" :
                                  (encType == WIFI_AUTH_WPA_WPA2_PSK) ? "WPA/WPA2" : "其他";
                break;
            }
        }
        
        if (!networkFound) {
            DEBUG_ERROR_PRINT("[WiFiManager] 錯誤：找不到網路 '%s'，可能信號太弱或名稱錯誤\n", ssid.c_str());
            WiFi.scanDelete(); // 清理掃描結果
            return false;
        }
        
        DEBUG_INFO_PRINT("[WiFiManager] 找到網路 '%s' - 信號: %d dBm, 加密: %s\n", 
                         ssid.c_str(), networkRSSI, networkEncryption.c_str());
        WiFi.scanDelete(); // 清理掃描結果
        
        WiFi.begin(ssid.c_str(), password.c_str());
        
        unsigned long startTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startTime < CONNECTION_TIMEOUT) {
            delay(100); // 減少延遲從500ms到100ms
            DEBUG_VERBOSE_PRINT(".");
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            DEBUG_INFO_PRINT("\n[WiFiManager] WiFi 連接成功！IP: %s\n", WiFi.localIP().toString().c_str());
            connectionAttempts = 0;
            
            // WiFi 連接成功後也啟動 web 服務器
            if (!webServer) {
                // 認證功能已暫時移除
                startWebServer();
                DEBUG_INFO_PRINT("[WiFiManager] 在 STA 模式下啟動 Web 服務器\n");
            }
            
            return true;
        }
        
        connectionAttempts++;
        
        // 詳細的錯誤診斷
        wl_status_t status = WiFi.status();
        DEBUG_ERROR_PRINT("\n[WiFiManager] WiFi 連接失敗 (嘗試 %d/%d)，狀態碼：%d\n", 
                         connectionAttempts, MAX_CONNECTION_ATTEMPTS, status);
        
        switch(status) {
            case WL_NO_SSID_AVAIL:
                DEBUG_ERROR_PRINT("[WiFiManager] 錯誤：找不到 SSID '%s'\n", ssid.c_str());
                break;
            case WL_CONNECT_FAILED:
                DEBUG_ERROR_PRINT("[WiFiManager] 錯誤：連接失敗，可能是密碼錯誤或網路問題\n");
                DEBUG_ERROR_PRINT("[WiFiManager] SSID: '%s', 密碼長度: %d\n", ssid.c_str(), password.length());
                break;
            case WL_CONNECTION_LOST:
                DEBUG_ERROR_PRINT("[WiFiManager] 錯誤：連接丟失\n");
                break;
            case WL_DISCONNECTED:
                DEBUG_ERROR_PRINT("[WiFiManager] 錯誤：未連接\n");
                break;
            case WL_IDLE_STATUS:
                DEBUG_ERROR_PRINT("[WiFiManager] 錯誤：連接空閒狀態\n");
                break;
            default:
                DEBUG_ERROR_PRINT("[WiFiManager] 錯誤：未知狀態 %d\n", status);
                break;
        }
        
        return false;
    }
    
    // 啟動 AP 模式
    bool startAPMode() {
        DEBUG_INFO_PRINT("[WiFiManager] 啟動 AP 模式...\n");
        
        // 先斷開任何現有連接
        WiFi.disconnect(true);
        delay(100);
        
        // 設置AP模式並增強兼容性
        WiFi.mode(WIFI_AP);
        
        // 等待模式切換完成
        delay(100);
        
        // 設置 WiFi 功率和協議以提高兼容性
        WiFi.setTxPower(WIFI_POWER_19_5dBm);  // 設置較高的發射功率
        
        // 確保使用兼容的協議
        esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
        
        // 配置IP地址
        IPAddress apIP(192, 168, 4, 1);
        IPAddress gateway(192, 168, 4, 1);  
        IPAddress subnet(255, 255, 255, 0);
        
        if (!WiFi.softAPConfig(apIP, gateway, subnet)) {
            DEBUG_ERROR_PRINT("[WiFiManager] AP IP 配置失敗\n");
        }
        
        // 使用更兼容的AP配置
        // 參數：SSID, 密碼, 頻道, 隱藏, 最大連接數, FTM響應器
        if (!WiFi.softAP(AP_SSID, nullptr, 6, 0, 8, false)) {  // 頻道6, 開放網路, 最大8連接
            DEBUG_ERROR_PRINT("[WiFiManager] AP 模式啟動失敗\n");
            return false;
        }
        
        // 等待AP完全穩定
        delay(1000);
        
        // 驗證AP狀態
        IPAddress apActualIP = WiFi.softAPIP();
        wifi_mode_t currentMode = WiFi.getMode();
        
        DEBUG_INFO_PRINT("[WiFiManager] AP 模式已啟動\n");
        DEBUG_INFO_PRINT("SSID: %s\n", AP_SSID);
        DEBUG_INFO_PRINT("頻道: 6, 開放網路 (無密碼)\n");
        DEBUG_INFO_PRINT("IP: %s\n", apActualIP.toString().c_str());
        DEBUG_INFO_PRINT("WiFi 模式: %d (1=STA, 2=AP, 3=STA+AP)\n", currentMode);
        DEBUG_INFO_PRINT("最大連接數: 8\n");
        DEBUG_INFO_PRINT("MAC 地址: %s\n", WiFi.softAPmacAddress().c_str());
        
        // 檢查AP是否真的可用
        if (apActualIP == IPAddress(0, 0, 0, 0)) {
            DEBUG_ERROR_PRINT("[WiFiManager] 警告：AP IP 地址為 0.0.0.0，可能配置失敗\n");
        }
        
        isAPMode = true;
        startWebServer();
        startDNSServer();
        
        return true;
    }
    
    // 啟動 Web 服務器
    void startWebServer() {
        if (webServer) delete webServer;
        webServer = new WebServer(80);
        
        // 主頁
        webServer->on("/", [this]() {
            handleRoot();
        });
        
        // 設定頁面
        webServer->on("/config", [this]() {
            handleConfig();
        });
        
        // 保存配置
        webServer->on("/save", HTTP_POST, [this]() {
            handleSave();
        });
        
        
        // 掃描 WiFi 網路
        webServer->on("/scan", [this]() {
            handleScan();
        });
        
        // OTA 狀態
        webServer->on("/ota-status", [this]() {
            handleOTA();
        });
        
        // 系統日誌
        webServer->on("/logs", [this]() {
            handleLogs();
        });
        
        // 清除日誌
        webServer->on("/logs-clear", HTTP_POST, [this]() {
            handleClearLogs();
        });
        
        // 日誌 API
        webServer->on("/api/logs", [this]() {
            handleLogsAPI();
        });
        
        // 重啟
        webServer->on("/restart", [this]() {
            handleRestart();
        });
        
        // 清除WiFi配置（調試用）
        webServer->on("/clear-wifi", [this]() {
            handleClearWiFi();
        });
        
        // 處理常見的瀏覽器請求
        webServer->on("/favicon.ico", [this]() {
            webServer->send(404, "text/plain", "Not Found");
        });
        
        webServer->on("/apple-touch-icon.png", [this]() {
            webServer->send(404, "text/plain", "Not Found");
        });
        
        webServer->on("/robots.txt", [this]() {
            webServer->send(200, "text/plain", "User-agent: *\nDisallow: /");
        });
        
        // 404 處理（重定向到主頁）
        webServer->onNotFound([this]() {
            String uri = webServer->uri();
            DEBUG_INFO_PRINT("[WiFiManager] 404請求: %s\n", uri.c_str());
            webServer->sendHeader("Location", "/", true);
            webServer->send(302, "text/plain", "");
        });
        
        webServer->begin();
        DEBUG_INFO_PRINT("[WiFiManager] Web 服務器已啟動: http://%s\n", WiFi.softAPIP().toString().c_str());
    }
    
    // 啟動 DNS 服務器
    void startDNSServer() {
        if (dnsServer) delete dnsServer;
        dnsServer = new DNSServer();
        IPAddress apIP(192, 168, 4, 1);
        dnsServer->start(53, "*", apIP);
        DEBUG_INFO_PRINT("[WiFiManager] DNS 服務器已啟動\n");
    }
    
    // 處理主頁請求
    void handleRoot() {
        String html = getMainPageHTML();
        webServer->send(200, "text/html", html);
    }
    
    // 處理配置頁面請求
    void handleConfig() {
        String html = getConfigPageHTML();
        webServer->send(200, "text/html", html);
    }
    
    // 驗證 HomeKit 配對碼
    bool isValidPairingCode(const String& code) {
        if (code.length() != 8) return false;
        
        // 檢查是否全為數字
        for (int i = 0; i < 8; i++) {
            if (!isDigit(code[i])) return false;
        }
        
        // 檢查是否為簡單模式（太容易猜測）
        if (code == "12345678" || code == "87654321" || 
            code == "00000000" || code == "11111111" ||
            code == "22222222" || code == "33333333" ||
            code == "44444444" || code == "55555555" ||
            code == "66666666" || code == "77777777" ||
            code == "88888888" || code == "99999999" ||
            code == "01234567" || code == "76543210") {
            return false;
        }
        
        return true;
    }
    
    // URL 解碼函數
    String urlDecode(const String& encoded) {
        String decoded = "";
        for (size_t i = 0; i < encoded.length(); i++) {
            if (encoded[i] == '%' && i + 2 < encoded.length()) {
                // 解析十六進制字符
                char hex[3] = {encoded[i+1], encoded[i+2], '\0'};
                char* endPtr;
                long value = strtol(hex, &endPtr, 16);
                if (*endPtr == '\0') {
                    decoded += (char)value;
                    i += 2; // 跳過已處理的字符
                } else {
                    decoded += encoded[i]; // 無效的編碼，保持原樣
                }
            } else if (encoded[i] == '+') {
                decoded += ' '; // '+' 代表空格
            } else {
                decoded += encoded[i];
            }
        }
        return decoded;
    }

    // 處理保存配置請求
    void handleSave() {
        String ssid = webServer->arg("ssid");
        String password = webServer->arg("password");
        String deviceName = webServer->arg("deviceName");
        String pairingCode = webServer->arg("pairingCode");
        
        // URL 解碼處理
        ssid = urlDecode(ssid);
        password = urlDecode(password);
        deviceName = urlDecode(deviceName);
        
        // 清理字符串
        ssid.trim();
        password.trim();
        deviceName.trim();
        pairingCode.trim();
        
        DEBUG_INFO_PRINT("[WiFiManager] 保存配置: SSID=%s, 密碼長度=%d\n", 
                         ssid.c_str(), password.length());
        
        // 驗證配對碼
        if (pairingCode.length() > 0 && !isValidPairingCode(pairingCode)) {
            String html = getErrorPageHTML("配對碼無效", 
                "HomeKit 配對碼必須是8位數字，且不能是簡單的序列（如12345678）。<br>"
                "建議使用複雜的數字組合，例如：11122333");
            webServer->send(400, "text/html", html);
            return;
        }
        
        // 驗證 WiFi 配置
        if (ssid.length() == 0) {
            String html = getErrorPageHTML("SSID 無效", "SSID 不能為空");
            webServer->send(400, "text/html", html);
            return;
        }
        
        if (ssid.length() > 32) {
            String html = getErrorPageHTML("SSID 過長", "SSID 長度不能超過 32 個字符");
            webServer->send(400, "text/html", html);
            return;
        }
        
        if (password.length() > 63) {
            String html = getErrorPageHTML("密碼過長", "WiFi 密碼長度不能超過 63 個字符");
            webServer->send(400, "text/html", html);
            return;
        }
        
        // 檢查密碼中的不可見字符
        bool hasInvalidChars = false;
        for (size_t i = 0; i < password.length(); i++) {
            if (password[i] < 32 && password[i] != 9) { // 允許 Tab 字符
                hasInvalidChars = true;
                break;
            }
        }
        
        if (hasInvalidChars) {
            String html = getErrorPageHTML("密碼包含無效字符", 
                "密碼包含不可見字符，請重新輸入");
            webServer->send(400, "text/html", html);
            return;
        }
        
        // 保存 WiFi 配置
        if (ssid.length() > 0) {
            config.setWiFiCredentials(ssid, password);
            DEBUG_INFO_PRINT("[WiFiManager] 配置驗證通過，SSID: '%s', 密碼長度: %d\n", 
                           ssid.c_str(), password.length());
        }
        
        // 保存 HomeKit 配置
        if (deviceName.length() > 0) {
            String finalPairingCode = (pairingCode.length() > 0 && isValidPairingCode(pairingCode)) ? 
                                     pairingCode : "11122333";
            config.setHomeKitConfig(finalPairingCode, deviceName, "HSPN");
        }
        
        String html = getSavePageHTML(ssid);
        webServer->send(200, "text/html", html);
        
        // 使用安全重啟（減少延遲）
        delay(1000); // 減少從2秒到1秒
        safeRestart();
    }
    
    
    // 處理掃描 WiFi 請求
    void handleScan() {
        // 設置更長的超時時間，避免客戶端斷開
        webServer->sendHeader("Connection", "keep-alive");
        webServer->sendHeader("Cache-Control", "no-cache");
        
        DEBUG_INFO_PRINT("[WiFiManager] 處理掃描請求...\n");
        String networks = scanWiFiNetworks();
        webServer->send(200, "application/json", networks);
        DEBUG_INFO_PRINT("[WiFiManager] 掃描請求處理完成\n");
    }
    
    // 處理 OTA 狀態請求
    void handleOTA() {
        String html = getOTAPageHTML();
        webServer->sendHeader("Content-Type", "text/html; charset=utf-8");
        webServer->send(200, "text/html", html);
    }
    
    // 處理日誌頁面請求
    void handleLogs() {
        // 使用簡化版本以提高性能
        String html = getSimpleLogHTML();
        webServer->sendHeader("Content-Type", "text/html; charset=utf-8");
        webServer->send(200, "text/html", html);
    }
    
    // 簡化的日誌頁面
    String getSimpleLogHTML() {
        auto stats = LOG_MANAGER.getStats();
        auto logs = LOG_MANAGER.getLogs();
        
        // 建構日誌內容
        String logContent = "";
        if (logs.size() > 0) {
            // 顯示最新的10條日誌
            size_t startIndex = logs.size() > 10 ? logs.size() - 10 : 0;
            size_t shownEntries = logs.size() - startIndex;
            for (size_t i = startIndex; i < logs.size(); i++) {
                const auto& entry = logs[i];
                String timeStr = String(entry.timestamp / 1000) + "." + String(entry.timestamp % 1000);
                logContent += "<div class=\"log-entry\">";
                logContent += "[" + timeStr + "] [" + String(LOG_MANAGER.getLevelString(entry.level)) + "] [" + entry.component + "] " + entry.message;
                logContent += "</div>";
            }
        }
        
        return WebUI::getLogPage(logContent, "/logs-clear", "/api/logs", 
                                stats.totalEntries, stats.infoCount, stats.warningCount, 
                                stats.errorCount, logs.size() > 10 ? 10 : logs.size());
    }
    
    // 處理清除日誌請求
    void handleClearLogs() {
        LOG_MANAGER.clearLogs();
        LOG_INFO("WiFiManager", "日誌已被清除");
        webServer->send(200, "text/plain", "OK");
    }
    
    // 處理日誌 API 請求
    void handleLogsAPI() {
        String json = LOG_MANAGER.generateLogJSON();
        webServer->sendHeader("Content-Type", "application/json; charset=utf-8");
        webServer->send(200, "application/json", json);
    }
    
    // 處理重啟請求
    void handleRestart() {
        String html = WebUI::getRestartPage();
        webServer->sendHeader("Content-Type", "text/html; charset=utf-8");
        webServer->send(200, "text/html", html);
        delay(500); // 減少延遲從1秒到500ms
        safeRestart();
    }
    
    // 處理清除WiFi配置請求（調試用）
    void handleClearWiFi() {
        config.clearWiFiConfig();
        String html = "<html><body><h1>WiFi 配置已清除</h1>";
        html += "<p>設備將重新啟動並進入配置模式</p>";
        html += "<script>setTimeout(function(){window.location.href='/';}, 3000);</script>";
        html += "</body></html>";
        webServer->send(200, "text/html", html);
        delay(1000);
        safeRestart();
    }
    
    // 掃描 WiFi 網路 - 高性能版本，使用緩存
    String scanWiFiNetworks() {
        unsigned long currentTime = millis();
        
        // 檢查是否可以使用緩存結果
        if (currentTime - lastNetworkScan < NETWORK_SCAN_CACHE_TIME && cachedNetworksJSON.length() > 0) {
            DEBUG_INFO_PRINT("[WiFiManager] 使用緩存的網路掃描結果\n");
            return cachedNetworksJSON;
        }
        
        DEBUG_INFO_PRINT("[WiFiManager] 開始新的網路掃描...\n");
        lastNetworkScan = currentTime;
        
        int n;
        
        // 在 AP 模式下使用優化的掃描方式
        if (isAPMode) {
            // 減少重試次數，提高效率
            n = WiFi.scanNetworks(false, false, false, 2000); // 2秒超時
            
            if (n <= 0 || n == WIFI_SCAN_FAILED) {
                DEBUG_WARN_PRINT("[WiFiManager] 掃描失敗，返回空列表\n");
                cachedNetworksJSON = "[]";
                return "[]";
            }
        } else {
            WiFi.scanDelete();
            n = WiFi.scanNetworks(false, true);
        }
        
        DEBUG_INFO_PRINT("[WiFiManager] 掃描完成，找到 %d 個網路\n", n);
        
        if (n == WIFI_SCAN_FAILED || n < 0) {
            cachedNetworksJSON = "[]";
            return "[]";
        }
        
        if (n == 0) {
            cachedNetworksJSON = "[]";
            return "[]";
        }
        
        // 高效構建JSON - 預估容量
        String json;
        json.reserve(n * 80); // 預估每個網路約80字符
        json = "[";
        
        int validNetworks = 0;
        const int maxNetworks = 12; // 減少最大網路數量
        
        for (int i = 0; i < n && validNetworks < maxNetworks; ++i) {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) continue; // 跳過隱藏網路
            
            if (validNetworks > 0) json += ",";
            json += "{\"ssid\":\"";
            json += ssid;
            json += "\",\"rssi\":";
            json += WiFi.RSSI(i);
            json += ",\"secure\":";
            json += (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) ? "true" : "false";
            json += "}";
            validNetworks++;
        }
        
        json += "]";
        cachedNetworksJSON = json;
        DEBUG_INFO_PRINT("[WiFiManager] 返回 %d 個有效網路並緩存\n", validNetworks);
        return json;
    }
    
    // 檢查狀態並處理
    void loop() {
        // 處理 web 服務器請求（無論是 AP 還是 STA 模式）
        if (webServer) {
            webServer->handleClient();
        }
        
        if (isAPMode) {
            // 處理 DNS 請求
            if (dnsServer) dnsServer->processNextRequest();
            
            // 檢查 AP 狀態並維持穩定性
            static unsigned long lastAPCheck = 0;
            if (millis() - lastAPCheck > 10000) { // 每10秒檢查一次
                lastAPCheck = millis();
                
                // 檢查 AP 是否仍在運行
                if (WiFi.getMode() != WIFI_AP && WiFi.getMode() != WIFI_AP_STA) {
                    DEBUG_WARN_PRINT("[WiFiManager] 檢測到 AP 模式異常，嘗試重新啟動\n");
                    startAPMode(); // 重新啟動 AP 模式
                } else {
                    DEBUG_VERBOSE_PRINT("[WiFiManager] AP 模式運行正常，已連接客戶端: %d\n", WiFi.softAPgetStationNum());
                }
            }
        } else {
            // 改進的 WiFi 連接狀態檢查 - 更智能的重連策略
            unsigned long currentTime = millis();
            
            // 主要穩定性檢查 - 降低檢查頻率
            if (currentTime - lastWiFiStabilityCheck >= WIFI_STABILITY_CHECK_INTERVAL) {
                lastWiFiStabilityCheck = currentTime;
                
                if (WiFi.status() != WL_CONNECTED) {
                    consecutiveFailures++;
                    DEBUG_WARN_PRINT("[WiFiManager] WiFi斷線檢測 - 連續失敗: %d次\n", consecutiveFailures);
                    
                    // 立即嘗試重連，不等待多次失敗
                    if (consecutiveFailures >= 1 && 
                        currentTime - lastConnectionAttempt >= WIFI_RECONNECT_INTERVAL) {
                        
                        lastConnectionAttempt = currentTime;
                        String ssid = config.getWiFiSSID();
                        String password = config.getWiFiPassword();
                        
                        DEBUG_INFO_PRINT("[WiFiManager] 嘗試WiFi重連 (失敗%d次後)\n", consecutiveFailures);
                        
                        if (connectToWiFi(ssid, password)) {
                            consecutiveFailures = 0; // 重連成功，重置計數器
                            wifiStabilityMode = false;
                        } else {
                            // 重連失敗，進入穩定模式
                            if (consecutiveFailures >= MAX_CONNECTION_ATTEMPTS) {
                                DEBUG_ERROR_PRINT("[WiFiManager] 多次重連失敗，切換到 AP 模式\n");
                                wifiStabilityMode = true;
                                startAPMode();
                            }
                        }
                    }
                } else {
                    // WiFi連接正常
                    if (consecutiveFailures > 0) {
                        DEBUG_INFO_PRINT("[WiFiManager] WiFi連接恢復正常\n");
                        consecutiveFailures = 0;
                        wifiStabilityMode = false;
                    }
                    
                    // 檢查信號強度，只在信號極弱時才考慮重連
                    int rssi = WiFi.RSSI();
                    if (rssi < WIFI_SIGNAL_THRESHOLD && !wifiStabilityMode) {
                        DEBUG_WARN_PRINT("[WiFiManager] WiFi信號弱 (%d dBm)，暫時進入穩定模式\n", rssi);
                        wifiStabilityMode = true; // 臨時進入穩定模式，避免頻繁重連
                    } else if (rssi > WIFI_SIGNAL_THRESHOLD + 10 && wifiStabilityMode) {
                        DEBUG_INFO_PRINT("[WiFiManager] WiFi信號恢復 (%d dBm)，退出穩定模式\n", rssi);
                        wifiStabilityMode = false;
                    }
                }
            }
        }
    }
    
    // 獲取狀態
    bool isConnected() const {
        return !isAPMode && WiFi.status() == WL_CONNECTED;
    }
    
    bool isInAPMode() const {
        return isAPMode;
    }
    
    String getIP() const {
        if (isAPMode) {
            return WiFi.softAPIP().toString();
        } else {
            return WiFi.localIP().toString();
        }
    }
    
    // 停止 AP 模式並切換到 STA 模式
    void stopAPMode() {
        if (!isAPMode) return;
        
        DEBUG_INFO_PRINT("[WiFiManager] 停止 AP 模式...\n");
        
        // 停止 DNS 服務器
        if (dnsServer) {
            dnsServer->stop();
            delete dnsServer;
            dnsServer = nullptr;
        }
        
        // 停止 Web 服務器
        if (webServer) {
            webServer->stop();
            delete webServer;
            webServer = nullptr;
        }
        
        // 停止 SoftAP
        WiFi.softAPdisconnect(true);
        
        // 切換到 STA 模式
        WiFi.mode(WIFI_STA);
        delay(100);
        
        isAPMode = false;
        DEBUG_INFO_PRINT("[WiFiManager] AP 模式已停止\n");
    }
    
    // 生成 Web 認證憑證
    void generateWebCredentials() {
        String mac = WiFi.macAddress();
        mac.replace(":", "");
        mac.toLowerCase();
        
        webAuthUser = "admin";
        webAuthPassword = "DS" + mac.substring(6);
        
        DEBUG_INFO_PRINT("[WiFiManager] 原始MAC: %s\n", WiFi.macAddress().c_str());
        DEBUG_INFO_PRINT("[WiFiManager] 處理後MAC: %s\n", mac.c_str());
        DEBUG_INFO_PRINT("[WiFiManager] MAC後6位: %s\n", mac.substring(6).c_str());
        DEBUG_INFO_PRINT("[WiFiManager] 生成 Web 認證憑證 - 用戶: %s, 密碼: %s\n", 
                         webAuthUser.c_str(), webAuthPassword.c_str());
    }
    
    // Web 基本認證
    bool authenticate() {
        if (!webServer->authenticate(webAuthUser.c_str(), webAuthPassword.c_str())) {
            webServer->requestAuthentication(BASIC_AUTH, "DaiSpan Configuration", "請輸入認證憑證");
            return false;
        }
        return true;
    }
    
    // 獲取認證信息
    String getAuthInfo() const {
        return "用戶名: " + webAuthUser + " | 密碼: " + webAuthPassword;
    }

private:
    String cachedNetworksJSON; // 網路掃描緩存
    // 生成主頁 HTML
    String getMainPageHTML() {
        String html = WebUI::getPageHeader("DaiSpan 配置");
        html += "<div class=\"container\">";
        html += "<h1>🌡️ DaiSpan 智能恆溫器</h1>";
        
        // 狀態資訊
        html += "<div class=\"status\">";
        if (isAPMode) {
            html += "<h3>📡 配置狀態</h3>";
            html += "<p>設備已啟動 AP 配置模式</p>";
            html += "<p>請連接此設備並配置 WiFi 設定</p>";
            html += "<p><strong>AP SSID:</strong> " + String(AP_SSID) + "</p>";
            html += "<p><strong>IP地址:</strong> " + WiFi.softAPIP().toString() + "</p>";
        } else {
            html += "<h3>🌐 系統狀態</h3>";
            html += "<p>設備已連接到 WiFi: <strong>" + WiFi.SSID() + "</strong></p>";
            html += "<p>IP 地址: <strong>" + WiFi.localIP().toString() + "</strong></p>";
            html += "<p>信號強度: " + String(WiFi.RSSI()) + " dBm</p>";
        }
        html += "</div>";
        
        // 導航按鈕
        html += "<div style=\"text-align: center;\">";
        html += "<a href=\"/config\" class=\"button\">📡 WiFi 設定</a>";
        html += "<a href=\"/ota-status\" class=\"button\">🔄 OTA 更新</a>";
        html += "<a href=\"/logs\" class=\"button\">📊 系統日誌</a>";
        html += "<a href=\"/restart\" class=\"button\">🔄 重新啟動</a>";
        html += "</div>";
        
        html += "</div>";
        html += WebUI::getPageFooter();
        return html;
    }
    
    // 生成配置頁面 HTML
    String getConfigPageHTML() {
        String html = WebUI::getPageHeader("WiFi 配置");
        html += "<div class=\"container\">";
        html += "<h1>📡 WiFi 配置</h1>";
        
        html += "<form action=\"/save\" method=\"post\">";
        
        // WiFi 網路列表和配置表單
        html += WebUI::getWiFiNetworkList("networks");
        html += WebUI::getWiFiConfigForm();
        
        // HomeKit 配置表單
        html += WebUI::getHomeKitConfigForm();
        
        html += "<button type=\"submit\" class=\"button\">💾 保存配置</button>";
        html += "</form>";
        
        // 返回按鈕
        html += "<div style=\"text-align: center; margin-top: 20px;\">";
        html += "<a href=\"/\" class=\"button secondary\">⬅️ 返回主頁</a>";
        html += "</div>";
        
        html += "</div>";
        
        // 添加 WiFi 掃描腳本
        html += WebUI::getWiFiScanScript();
        
        html += WebUI::getPageFooter();
        return html;
    }
    
    // 生成保存頁面 HTML
    String getSavePageHTML(const String& ssid) {
        String message = "WiFi 配置已成功保存！<br>";
        message += "正在重新啟動並嘗試連接到: <strong>" + ssid + "</strong>";
        return WebUI::getSuccessPage("配置已保存", message, 3);
    }
    
    // 生成錯誤頁面 HTML
    String getErrorPageHTML(const String& title, const String& message) {
        return WebUI::getErrorPage(title, message, "/config");
    }

    // 生成 OTA 頁面 HTML
    String getOTAPageHTML() {
        #ifdef ENABLE_OTA_UPDATE
        String otaStatus = "<p><span style=\"color: green;\">●</span> OTA 服務已啟用，請使用 PlatformIO 或 Arduino IDE 進行更新。</p>";
        #else
        String otaStatus = "<p><span style=\"color: red;\">●</span> OTA 功能未啟用。</p>";
        #endif
        return WebUI::getOTAPage(WiFi.localIP().toString(), "DaiSpan-Thermostat", otaStatus);
    }
};
