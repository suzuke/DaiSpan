#pragma once

#include "WiFi.h"
#include "WebServer.h"
#include "DNSServer.h"
#include "Update.h"
#include "Config.h"
#include "Debug.h"
#include "OTAManager.h"
#include "LogManager.h"

// 前向聲明
extern void safeRestart();

class WiFiManager {
private:
    ConfigManager& config;
    WebServer* webServer;
    DNSServer* dnsServer;
    OTAManager* otaManager;
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
    static constexpr int CONNECTION_TIMEOUT = 10000; // 10 秒
    
public:
    WiFiManager(ConfigManager& cfg) : 
        config(cfg), 
        webServer(nullptr), 
        dnsServer(nullptr),
        otaManager(nullptr),
        isAPMode(false), 
        isConfigured(false),
        connectionAttempts(0),
        lastConnectionAttempt(0) {
        // 延遲到 WiFi 初始化後再生成認證憑證
    }
    
    ~WiFiManager() {
        if (webServer) delete webServer;
        if (dnsServer) delete dnsServer;
        // OTA 管理器由外部管理，不在此處刪除
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
        
        // 嘗試連接已配置的 WiFi
        if (connectToWiFi(ssid, password)) {
            this->isConfigured = true;
            return true;
        }
        
        // 連接失敗，啟動 AP 模式
        DEBUG_ERROR_PRINT("[WiFiManager] WiFi 連接失敗，啟動 AP 模式\n");
        return startAPMode();
    }
    
    // 嘗試連接 WiFi
    bool connectToWiFi(const String& ssid, const String& password) {
        DEBUG_INFO_PRINT("[WiFiManager] 嘗試連接 WiFi: %s\n", ssid.c_str());
        
        WiFi.mode(WIFI_STA);
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
        DEBUG_ERROR_PRINT("\n[WiFiManager] WiFi 連接失敗 (嘗試 %d/%d)\n", 
                         connectionAttempts, MAX_CONNECTION_ATTEMPTS);
        return false;
    }
    
    // 啟動 AP 模式
    bool startAPMode() {
        DEBUG_INFO_PRINT("[WiFiManager] 啟動 AP 模式...\n");
        
        WiFi.mode(WIFI_AP);
        IPAddress apIP(192, 168, 4, 1);
        WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
        
        if (!WiFi.softAP(AP_SSID)) {
            DEBUG_ERROR_PRINT("[WiFiManager] AP 模式啟動失敗\n");
            return false;
        }
        
        DEBUG_INFO_PRINT("[WiFiManager] AP 模式已啟動\n");
        DEBUG_INFO_PRINT("SSID: %s\n", AP_SSID);
        DEBUG_INFO_PRINT("開放網路 (無密碼)\n");
        DEBUG_INFO_PRINT("IP: %s\n", WiFi.softAPIP().toString().c_str());
        
        // 認證功能已暫時移除
        
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
        
        // 404 處理（重定向到主頁）
        webServer->onNotFound([this]() {
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
    
    // 處理保存配置請求
    void handleSave() {
        String ssid = webServer->arg("ssid");
        String password = webServer->arg("password");
        String deviceName = webServer->arg("deviceName");
        String pairingCode = webServer->arg("pairingCode");
        
        DEBUG_INFO_PRINT("[WiFiManager] 保存配置: SSID=%s\n", ssid.c_str());
        
        // 驗證配對碼
        if (pairingCode.length() > 0 && !isValidPairingCode(pairingCode)) {
            String html = getErrorPageHTML("配對碼無效", 
                "HomeKit 配對碼必須是8位數字，且不能是簡單的序列（如12345678）。<br>"
                "建議使用複雜的數字組合，例如：11122333");
            webServer->send(400, "text/html", html);
            return;
        }
        
        // 保存 WiFi 配置
        if (ssid.length() > 0) {
            config.setWiFiCredentials(ssid, password);
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
        String networks = scanWiFiNetworks();
        webServer->send(200, "application/json", networks);
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
        
        String html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">";
        html += "<title>DaiSpan Logs</title>";
        html += "<style>body{font-family:monospace;margin:20px;background:#f0f0f0;}";
        html += ".container{max-width:800px;margin:0 auto;background:white;padding:20px;border-radius:10px;}";
        html += "h1{color:#333;text-align:center;}.button{padding:8px 16px;margin:5px;background:#007cba;color:white;text-decoration:none;border-radius:5px;}";
        html += ".stats{background:#e8f4f8;padding:15px;border-radius:5px;margin:15px 0;}";
        html += ".log-container{background:#000;color:#00ff00;padding:15px;border-radius:5px;max-height:400px;overflow-y:auto;font-size:12px;}";
        html += ".log-entry{margin:2px 0;}";
        html += "</style></head><body><div class=\"container\">";
        html += "<h1>DaiSpan System Logs</h1>";
        html += "<div style=\"text-align:center;\">";
        html += "<a href=\"/api/logs\" class=\"button\">View JSON</a>";
        html += "<button onclick=\"clearLogs()\" class=\"button\">Clear</button>";
        html += "<a href=\"/\" class=\"button\">Back</a>";
        html += "</div>";
        html += "<div class=\"stats\"><h3>Statistics</h3>";
        html += "<p>Total: " + String(stats.totalEntries) + " entries";
        if (logs.size() < stats.totalEntries) {
            html += " (showing latest " + String(logs.size()) + ")";
        }
        html += "</p>";
        html += "<p>INFO: " + String(stats.infoCount) + " | WARNING: " + String(stats.warningCount) + " | ERROR: " + String(stats.errorCount) + "</p>";
        html += "</div>";
        
        // 顯示最新的日誌條目
        html += "<div class=\"log-container\">";
        if (logs.size() > 0) {
            // 顯示最新的10條日誌
            size_t startIndex = logs.size() > 10 ? logs.size() - 10 : 0;
            for (size_t i = startIndex; i < logs.size(); i++) {
                const auto& entry = logs[i];
                String timeStr = String(entry.timestamp / 1000) + "." + String(entry.timestamp % 1000);
                html += "<div class=\"log-entry\">";
                html += "[" + timeStr + "] [" + String(LOG_MANAGER.getLevelString(entry.level)) + "] [" + entry.component + "] " + entry.message;
                html += "</div>";
            }
        } else {
            html += "<p style=\"color:#666;\">No logs available</p>";
        }
        html += "</div>";
        
        html += "<p style=\"margin-top:15px;\"><strong>Note:</strong> Only latest 10 entries shown. Use <a href=\"/api/logs\" target=\"_blank\">JSON API</a> for complete logs.</p>";
        html += "<script>function clearLogs(){if(confirm('Clear all logs?')){fetch('/logs-clear',{method:'POST'}).then(()=>location.reload());}}</script>";
        html += "</div></body></html>";
        return html;
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
        String html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">";
        html += "<title>重新啟動</title>";
        html += "<style>body{font-family:Arial,sans-serif;text-align:center;margin:50px;background:#f0f0f0;}";
        html += ".container{max-width:400px;margin:0 auto;background:white;padding:30px;border-radius:10px;}";
        html += "h1{color:#333;}</style></head><body>";
        html += "<div class=\"container\"><h1>🔄 重新啟動中...</h1>";
        html += "<p>設備正在重新啟動，請稍候...</p>";
        html += "<p>重啟完成後請重新連接。</p></div></body></html>";
        
        webServer->sendHeader("Content-Type", "text/html; charset=utf-8");
        webServer->send(200, "text/html", html);
        delay(500); // 減少延遲從1秒到500ms
        safeRestart();
    }
    
    // 掃描 WiFi 網路
    String scanWiFiNetworks() {
        DEBUG_INFO_PRINT("[WiFiManager] 開始掃描 WiFi 網路...\n");
        
        // 在 AP 模式下掃描可能需要特殊處理
        WiFi.scanDelete(); // 清除之前的掃描結果
        int n = WiFi.scanNetworks(false, true); // 異步掃描，顯示隱藏網路
        
        DEBUG_INFO_PRINT("[WiFiManager] 掃描完成，找到 %d 個網路\n", n);
        
        if (n == -1) {
            DEBUG_ERROR_PRINT("[WiFiManager] WiFi 掃描失敗\n");
            return "[]";
        }
        
        if (n == 0) {
            DEBUG_INFO_PRINT("[WiFiManager] 未找到任何 WiFi 網路\n");
            return "[]";
        }
        
        String json = "[";
        int validNetworks = 0;
        
        for (int i = 0; i < n; ++i) {
            String ssid = WiFi.SSID(i);
            // 跳過空 SSID（隱藏網路）
            if (ssid.length() == 0) {
                continue;
            }
            
            if (validNetworks > 0) json += ",";
            json += "{";
            json += "\"ssid\":\"" + ssid + "\",";
            json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
            json += "\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false");
            json += "}";
            validNetworks++;
        }
        
        json += "]";
        DEBUG_INFO_PRINT("[WiFiManager] 返回 %d 個有效網路\n", validNetworks);
        return json;
    }
    
    // 檢查狀態並處理
    void loop() {
        // 處理 web 服務器請求（無論是 AP 還是 STA 模式）
        if (webServer) {
            webServer->handleClient();
        }
        
        if (isAPMode) {
            if (dnsServer) dnsServer->processNextRequest();
        } else {
            // 檢查 WiFi 連接狀態
            if (WiFi.status() != WL_CONNECTED && 
                millis() - lastConnectionAttempt > 30000) { // 30 秒檢查一次
                lastConnectionAttempt = millis();
                
                String ssid = config.getWiFiSSID();
                String password = config.getWiFiPassword();
                
                if (!connectToWiFi(ssid, password)) {
                    if (connectionAttempts >= MAX_CONNECTION_ATTEMPTS) {
                        DEBUG_ERROR_PRINT("[WiFiManager] 多次連接失敗，切換到 AP 模式\n");
                        startAPMode();
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
    
    // 設置 OTA 管理器
    void setOTAManager(OTAManager* ota) {
        otaManager = ota;
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
    // 生成主頁 HTML
    String getMainPageHTML() {
        String statusText = "";
        String authInfo = "";
        
        if (isAPMode) {
            statusText = "<h3>配置狀態</h3><p>設備已啟動 AP 配置模式</p><p>請連接此設備並配置 WiFi 設定</p>";
        } else {
            statusText = "<h3>系統狀態</h3><p>設備已連接到 WiFi: <strong>" + WiFi.SSID() + "</strong></p><p>IP 地址: <strong>" + WiFi.localIP().toString() + "</strong></p>";
        }
        
        return R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>DaiSpan 配置</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }
        .container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        h1 { color: #333; text-align: center; }
        .button { display: inline-block; padding: 10px 20px; margin: 10px 5px; background: #007cba; color: white; text-decoration: none; border-radius: 5px; border: none; cursor: pointer; }
        .button:hover { background: #005a8b; }
        .status { background: #e8f4f8; padding: 15px; border-radius: 5px; margin: 20px 0; }
        .auth-info { background: #fff3cd; border: 1px solid #ffeaa7; padding: 15px; border-radius: 5px; margin: 20px 0; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🌡️ DaiSpan 智能恆溫器</h1>
        <div class="status">
            )" + statusText + R"(
        </div>
        )" + authInfo + R"(
        <div style="text-align: center;">
            <a href="/config" class="button">📡 WiFi 設定</a>
            <a href="/ota-status" class="button">🔄 OTA 更新</a>
            <a href="/logs" class="button">📊 系統日誌</a>
            <a href="/restart" class="button">🔄 重新啟動</a>
        </div>
    </div>
</body>
</html>
        )";
    }
    
    // 生成配置頁面 HTML
    String getConfigPageHTML() {
        return R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>WiFi 配置</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }
        .container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        h1 { color: #333; text-align: center; }
        .form-group { margin: 15px 0; }
        label { display: block; margin-bottom: 5px; font-weight: bold; }
        input, select { width: 100%; padding: 10px; border: 1px solid #ddd; border-radius: 5px; box-sizing: border-box; }
        .button { display: inline-block; padding: 10px 20px; margin: 10px 5px; background: #007cba; color: white; text-decoration: none; border-radius: 5px; border: none; cursor: pointer; width: 100%; }
        .button:hover { background: #005a8b; }
        .network-list { max-height: 200px; overflow-y: auto; border: 1px solid #ddd; border-radius: 5px; }
        .network-item { padding: 10px; border-bottom: 1px solid #eee; cursor: pointer; }
        .network-item:hover { background: #f5f5f5; }
        .signal-strength { float: right; color: #666; }
    </style>
</head>
<body>
    <div class="container">
        <h1>📡 WiFi 配置</h1>
        <form action="/save" method="post">
            <div class="form-group">
                <label>可用網路:</label>
                <div class="network-list" id="networks">
                    <div style="text-align: center; padding: 20px;">載入中...</div>
                </div>
            </div>
            
            <div class="form-group">
                <label for="ssid">WiFi 名稱 (SSID):</label>
                <input type="text" id="ssid" name="ssid" required>
            </div>
            
            <div class="form-group">
                <label for="password">WiFi 密碼:</label>
                <input type="password" id="password" name="password">
            </div>
            
            <hr>
            
            <div class="form-group">
                <label for="deviceName">設備名稱:</label>
                <input type="text" id="deviceName" name="deviceName" value="智能恆溫器">
            </div>
            
            <div class="form-group">
                <label for="pairingCode">HomeKit 配對碼:</label>
                <input type="text" id="pairingCode" name="pairingCode" value="11122333" pattern="[0-9]{8}" title="請輸入8位數字">
                <small style="color: #666; font-size: 12px;">必須是8位數字，避免使用簡單序列（如12345678）</small>
            </div>
            
            <button type="submit" class="button">💾 保存配置</button>
        </form>
        
        <div style="text-align: center; margin-top: 20px;">
            <a href="/" class="button" style="background: #666;">⬅️ 返回主頁</a>
        </div>
    </div>

    <script>
        // 載入 WiFi 網路列表
        function loadNetworks() {
            const networkList = document.getElementById('networks');
            networkList.innerHTML = '<div style="text-align: center; padding: 20px;">正在掃描 WiFi 網路...</div>';
            
            fetch('/scan')
                .then(response => {
                    if (!response.ok) {
                        throw new Error('HTTP ' + response.status + ': ' + response.statusText);
                    }
                    return response.json();
                })
                .then(networks => {
                    networkList.innerHTML = '';
                    
                    if (networks.length === 0) {
                        networkList.innerHTML = '<div style="text-align: center; padding: 20px; color: orange;">未找到可用的 WiFi 網路</div>';
                        return;
                    }
                    
                    networks.forEach(network => {
                        const item = document.createElement('div');
                        item.className = 'network-item';
                        item.innerHTML = `
                            <strong>${network.ssid}</strong>
                            <span class="signal-strength">${network.rssi} dBm ${network.secure ? '🔒' : '🔓'}</span>
                        `;
                        item.onclick = () => {
                            document.getElementById('ssid').value = network.ssid;
                        };
                        networkList.appendChild(item);
                    });
                })
                .catch(error => {
                    console.error('WiFi scan error:', error);
                    var errorDiv = document.createElement('div');
                    errorDiv.style.textAlign = 'center';
                    errorDiv.style.padding = '20px';
                    errorDiv.style.color = 'red';
                    errorDiv.innerHTML = 'Load failed<br>';
                    var retryBtn = document.createElement('button');
                    retryBtn.textContent = 'Retry';
                    retryBtn.onclick = loadNetworks;
                    errorDiv.appendChild(retryBtn);
                    networkList.innerHTML = '';
                    networkList.appendChild(errorDiv);
                });
        }
        
        // 頁面載入時執行
        loadNetworks();
    </script>
</body>
</html>
        )";
    }
    
    // 生成保存頁面 HTML
    String getSavePageHTML(const String& ssid) {
        return R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>配置已保存</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }
        .container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); text-align: center; }
        h1 { color: #28a745; }
        .countdown { font-size: 24px; font-weight: bold; color: #007cba; }
    </style>
</head>
<body>
    <div class="container">
        <h1>✅ 配置已保存</h1>
        <p>WiFi 配置已成功保存！</p>
        <p>正在重新啟動並嘗試連接到: <strong>)" + ssid + R"(</strong></p>
        <div class="countdown" id="countdown">3</div>
        <p>秒後自動重啟...</p>
    </div>
    
    <script>
        let count = 3;
        const countdown = document.getElementById('countdown');
        const timer = setInterval(() => {
            count--;
            countdown.textContent = count;
            if (count <= 0) {
                clearInterval(timer);
                countdown.textContent = '重啟中...';
            }
        }, 1000);
    </script>
</body>
</html>
        )";
    }
    
    // 生成錯誤頁面 HTML
    String getErrorPageHTML(const String& title, const String& message) {
        return R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>配置錯誤</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }
        .container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); text-align: center; }
        h1 { color: #dc3545; }
        .message { background: #f8d7da; border: 1px solid #f5c6cb; color: #721c24; padding: 15px; border-radius: 5px; margin: 20px 0; }
        .button { display: inline-block; padding: 10px 20px; margin: 10px 5px; background: #007cba; color: white; text-decoration: none; border-radius: 5px; }
        .button:hover { background: #005a8b; }
    </style>
</head>
<body>
    <div class="container">
        <h1>❌ )" + title + R"(</h1>
        <div class="message">
            )" + message + R"(
        </div>
        <a href="/config" class="button">⬅️ 返回配置</a>
        <a href="/" class="button">🏠 回到主頁</a>
    </div>
</body>
</html>
        )";
    }

    // 生成 OTA 頁面 HTML
    String getOTAPageHTML() {
        String otaStatus = "";
        if (otaManager) {
            otaStatus = otaManager->getStatusHTML();
        } else {
            otaStatus = "<div class=\"ota-status\"><h3>🔄 OTA 更新狀態</h3><p><span style=\"color: red;\">●</span> OTA 管理器未初始化</p></div>";
        }
        
        return R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>OTA 更新狀態</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }
        .container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        h1 { color: #333; text-align: center; }
        .button { display: inline-block; padding: 10px 20px; margin: 10px 5px; background: #007cba; color: white; text-decoration: none; border-radius: 5px; border: none; cursor: pointer; }
        .button:hover { background: #005a8b; }
        .ota-status { background: #e8f4f8; padding: 15px; border-radius: 5px; margin: 20px 0; }
        .code-block { background: #f5f5f5; border: 1px solid #ddd; border-radius: 5px; padding: 15px; margin: 15px 0; font-family: monospace; white-space: pre-wrap; }
        .warning { background: #fff3cd; border: 1px solid #ffeaa7; border-radius: 5px; padding: 15px; margin: 15px 0; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🔄 OTA 遠程更新</h1>
        )" + otaStatus + R"(
        
        <div class="warning">
            <h3>⚠️ 注意事項</h3>
            <ul>
                <li>OTA 更新過程中請勿斷電或斷網</li>
                <li>更新失敗可能導致設備無法啟動</li>
                <li>建議在更新前備份當前固件</li>
                <li>更新完成後設備會自動重啟</li>
            </ul>
        </div>
        
        <div class="ota-instructions">
            <h3>📝 使用說明</h3>
            <p>使用 PlatformIO 進行 OTA 更新：</p>
            <div class="code-block">pio run -t upload --upload-port )" + WiFi.localIP().toString() + R"(</div>
            
            <p>或使用 Arduino IDE：</p>
            <ol>
                <li>工具 → 端口 → 選擇網路端口</li>
                <li>選擇設備主機名</li>
                <li>輸入 OTA 密碼</li>
                <li>點擊上傳</li>
            </ol>
        </div>
        
        <div style="text-align: center; margin-top: 30px;">
            <a href="/" class="button">⬅️ 返回主頁</a>
            <a href="/restart" class="button" style="background: #dc3545;">🔄 重新啟動</a>
        </div>
    </div>
</body>
</html>
        )";
    }
};