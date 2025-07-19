#pragma once

#include <Arduino.h>
#include <WiFi.h>

// 動態緩衝區大小計算
namespace WebUIMemory {
    inline size_t getAdaptiveBufferSize(size_t requestedSize) {
        uint32_t freeHeap = ESP.getFreeHeap();
        
        // 如果可用記憶體充足 (>50KB)，使用請求的大小
        if (freeHeap > 50000) {
            return requestedSize;
        }
        // 記憶體有限 (30-50KB)，使用較小的緩衝區
        else if (freeHeap > 30000) {
            return min(requestedSize, (size_t)(requestedSize * 0.75));
        }
        // 記憶體緊張 (20-30KB)，使用最小緩衝區
        else if (freeHeap > 20000) {
            return min(requestedSize, (size_t)(requestedSize * 0.5));
        }
        // 記憶體極度緊張 (<20KB)，使用極小緩衝區
        else {
            return min(requestedSize, (size_t)1024);
        }
    }
    
    inline bool canAllocateBuffer(size_t size) {
        return (ESP.getFreeHeap() > (size + 2048)); // 保留2KB余量
    }
}

/**
 * WebUI 共用模組
 * 提供統一的 Web 介面元件，供 WiFiManager 和 main.cpp 使用
 * 避免程式碼重複，統一 UI 風格
 */
namespace WebUI {

    // ==================== CSS 樣式 ====================
    
    /**
     * 取得極簡CSS樣式 (記憶體優化 - 單一壓縮版本)
     * 使用 PROGMEM 儲存以節省RAM
     */
    static const char* getCompactCSS() {
        static const char CSS_CONTENT[] PROGMEM = 
            "body{font-family:Arial;margin:10px;background:#f0f0f0}"
            ".container{max-width:600px;margin:0 auto;background:white;padding:15px;border-radius:5px}"
            "h1{color:#333;text-align:center}h2,h3{color:#333}"
            ".button{display:inline-block;padding:8px 15px;margin:5px;background:#007cba;color:white;text-decoration:none;border-radius:3px;border:none;cursor:pointer}"
            ".button:hover{background:#005a8b}.button.danger{background:#dc3545}.button.secondary{background:#666}"
            ".form-group{margin:10px 0}label{display:block;margin-bottom:3px;font-weight:bold}"
            "input,select{width:100%;padding:8px;border:1px solid #ddd;border-radius:3px;box-sizing:border-box}"
            "input:focus,select:focus{border-color:#007cba;outline:none}"
            ".status{background:#e8f4f8;padding:10px;border-radius:3px;margin:10px 0}"
            ".warning{background:#fff3cd;padding:10px;border-radius:3px;margin:10px 0}"
            ".info{background:#d1ecf1;padding:10px;border-radius:3px;margin:10px 0}"
            ".error{background:#f8d7da;color:#721c24;padding:10px;border-radius:3px;margin:10px 0}"
            ".status-card{background:#f8f9fa;border:1px solid #dee2e6;border-radius:5px;padding:15px;margin:15px 0}"
            ".status-item{display:flex;justify-content:space-between;align-items:center;padding:5px 0;border-bottom:1px solid #eee}"
            ".status-item:last-child{border-bottom:none}"
            ".status-label{font-weight:bold;color:#495057}.status-value{color:#6c757d}"
            ".status-good{color:#28a745}.status-warning{color:#ffc107}.status-error{color:#dc3545}"
            ".signal-strength{font-size:0.9em;color:#6c757d}"
            ".network-item{cursor:pointer;padding:8px;border:1px solid #ddd;margin:5px;border-radius:3px}"
            ".network-item:hover{background:#f8f9fa}";
        return CSS_CONTENT;
    }
    
    /**
     * 舊CSS函數保持向後兼容 (使用空實現)
     */
    String getCommonCSS() { return ""; }
    String getFormCSS() { return ""; }
    String getStatusCSS() { return ""; }
    String getNetworkCSS() { return ""; }
    String getSpecialCSS() { return ""; }


    // ==================== HTML 頁面結構 ====================

    /**
     * 取得頁面標頭
     */
    String getPageHeader(const String& title, bool includeRefresh = false, int refreshInterval = 0) {
        String header = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">";
        header += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
        header += "<title>" + title + "</title>";
        
        // 自動刷新功能
        if (includeRefresh && refreshInterval > 0) {
            header += "<meta http-equiv=\"refresh\" content=\"" + String(refreshInterval) + "\">";
        }
        
        // 使用壓縮的 CSS
        header += "<style>";
        header += String(getCompactCSS());
        header += "</style>";
        header += "</head><body>";
        
        return header;
    }

    /**
     * 取得頁面底部
     */
    String getPageFooter() {
        return "</body></html>";
    }

    /**
     * 取得導航列
     */
    String getNavigation(const String& currentPage = "", bool showBackButton = true) {
        String nav = "<div style=\"text-align: center; margin: 20px 0;\">";
        
        if (showBackButton) {
            nav += "<a href=\"/\" class=\"button secondary\">⬅️ 返回主頁</a>";
        }
        
        nav += "</div>";
        return nav;
    }

    // ==================== 系統狀態元件 (記憶體優化版本) ====================

    /**
     * 取得完整系統狀態卡片 (高性能流式版本)
     * 使用高效的字符串流方法，最小化記憶體分配
     */
    String getSystemStatusCard() {
        // 預估所需容量並使用String.reserve()
        String html;
        html.reserve(1400); // 根據實際內容預估容量
        
        // 預先計算資料以減少重複調用
        uint32_t freeHeap = ESP.getFreeHeap();
        unsigned long uptime = millis();
        
        // 網路連接卡片
        html += "<div class=\"status-card\"><h3>🌐 網路連接</h3>";
        if (WiFi.status() == WL_CONNECTED) {
            int rssi = WiFi.RSSI();
            const char* rssiClass = (rssi > -50) ? "status-good" : (rssi > -70) ? "status-warning" : "status-error";
            
            html += "<div class='status-item'><span class='status-label'>WiFi SSID:</span><span class='status-value status-good'>";
            html += WiFi.SSID();
            html += "</span></div><div class='status-item'><span class='status-label'>IP地址:</span><span class='status-value'>";
            html += WiFi.localIP().toString();
            html += "</span></div><div class='status-item'><span class='status-label'>MAC地址:</span><span class='status-value'>";
            html += WiFi.macAddress();
            html += "</span></div><div class='status-item'><span class='status-label'>信號強度:</span><span class='status-value ";
            html += rssiClass;
            html += "'>";
            html += rssi;
            html += " dBm</span></div><div class='status-item'><span class='status-label'>網關:</span><span class='status-value'>";
            html += WiFi.gatewayIP().toString();
            html += "</span></div>";
        } else {
            html += "<div class='status-item'><span class='status-label'>WiFi狀態:</span><span class='status-value status-error'>未連接</span></div>";
        }
        html += "</div>";

        // 系統資源卡片
        const char* heapClass = (freeHeap > 100000) ? "status-good" : (freeHeap > 50000) ? "status-warning" : "status-error";
        
        html += "<div class=\"status-card\"><h3>💻 系統資源</h3><div class='status-item'><span class='status-label'>可用記憶體:</span><span class='status-value ";
        html += heapClass;
        html += "'>";
        html += freeHeap;
        html += " bytes</span></div><div class='status-item'><span class='status-label'>晶片型號:</span><span class='status-value'>";
        html += ESP.getChipModel();
        html += "</span></div><div class='status-item'><span class='status-label'>CPU頻率:</span><span class='status-value'>";
        html += ESP.getCpuFreqMHz();
        html += " MHz</span></div><div class='status-item'><span class='status-label'>Flash大小:</span><span class='status-value'>";
        html += (ESP.getFlashChipSize() / 1048576);
        html += " MB</span></div><div class='status-item'><span class='status-label'>運行時間:</span><span class='status-value'>";
        
        // 高效的運行時間計算
        unsigned long days = uptime / 86400000UL;
        unsigned long hours = (uptime % 86400000UL) / 3600000UL;
        unsigned long minutes = (uptime % 3600000UL) / 60000UL;
        html += days;
        html += "天 ";
        html += hours;
        html += "時 ";
        html += minutes;
        html += "分</span></div><div class='status-item'><span class='status-label'>固件版本:</span><span class='status-value'>v3.0-OTA-FINAL</span></div></div>";

        return html;
    }

    // ==================== WiFi 網路元件 ====================

    /**
     * 取得 WiFi 網路列表 HTML
     */
    String getWiFiNetworkList(const String& elementId = "networks") {
        String html = "<div class=\"form-group\">";
        html += "<label>可用網路 <button type=\"button\" class=\"button\" onclick=\"rescanNetworks()\" style=\"font-size:12px;padding:5px 10px;margin-left:10px;\">🔄 重新掃描</button></label>";
        html += "<div class=\"network-list\" id=\"" + elementId + "\">";
        html += "<div style=\"text-align: center; padding: 20px;\">載入中...</div>";
        html += "</div>";
        html += "</div>";
        return html;
    }

    /**
     * 取得 WiFi 配置表單
     */
    String getWiFiConfigForm(const String& currentSSID = "") {
        String html = "<div class=\"form-group\">";
        html += "<label for=\"ssid\">WiFi 名稱 (SSID):</label>";
        html += "<input type=\"text\" id=\"ssid\" name=\"ssid\" value=\"" + currentSSID + "\" required>";
        html += "</div>";
        
        html += "<div class=\"form-group\">";
        html += "<label for=\"password\">WiFi 密碼:</label>";
        html += "<input type=\"password\" id=\"password\" name=\"password\">";
        html += "</div>";
        
        return html;
    }

    /**
     * 取得 HomeKit 配置表單
     */
    String getHomeKitConfigForm(const String& currentName = "智能恆溫器", const String& currentCode = "11122333", const String& currentQRID = "HSPN") {
        String html = "<hr>";
        html += "<h3>🏠 HomeKit 配置</h3>";
        
        html += "<div class=\"form-group\">";
        html += "<label for=\"deviceName\">設備名稱:</label>";
        html += "<input type=\"text\" id=\"deviceName\" name=\"deviceName\" value=\"" + currentName + "\">";
        html += "</div>";
        
        html += "<div class=\"form-group\">";
        html += "<label for=\"pairingCode\">HomeKit 配對碼:</label>";
        html += "<input type=\"text\" id=\"pairingCode\" name=\"pairingCode\" value=\"" + currentCode + "\" pattern=\"[0-9]{8}\" title=\"請輸入8位數字\">";
        html += "<small>必須是8位數字，避免使用簡單序列（如12345678）</small>";
        html += "</div>";
        
        html += "<div class=\"form-group\">";
        html += "<label for=\"qrId\">QR識別碼:</label>";
        html += "<input type=\"text\" id=\"qrId\" name=\"qrId\" value=\"" + currentQRID + "\" maxlength=\"4\">";
        html += "<small>QR碼中的設備識別碼，通常為4個字符</small>";
        html += "</div>";
        
        return html;
    }

    // ==================== JavaScript 功能 ====================

    /**
     * 取得 WiFi 掃描 JavaScript
     */
    String getWiFiScanScript(const String& scanEndpoint = "/scan", const String& networkListId = "networks") {
        return R"(
            <script>
            function loadNetworks() {
                const networkList = document.getElementById(')" + networkListId + R"(');
                networkList.innerHTML = '<div style="text-align: center; padding: 20px;">正在掃描 WiFi 網路...<br><small>這可能需要幾秒鐘</small></div>';
                
                // 增加超時處理
                const controller = new AbortController();
                const timeoutId = setTimeout(() => controller.abort(), 15000); // 15秒超時
                
                fetch(')" + scanEndpoint + R"(', {
                    signal: controller.signal,
                    cache: 'no-cache'
                })
                    .then(response => {
                        clearTimeout(timeoutId);
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
                        clearTimeout(timeoutId);
                        console.error('WiFi scan error:', error);
                        var errorDiv = document.createElement('div');
                        errorDiv.style.textAlign = 'center';
                        errorDiv.style.padding = '20px';
                        errorDiv.style.color = 'red';
                        
                        if (error.name === 'AbortError') {
                            errorDiv.innerHTML = '掃描超時<br><small>請檢查設備是否正常工作</small><br>';
                        } else {
                            errorDiv.innerHTML = '載入失敗: ' + error.message + '<br>';
                        }
                        
                        var retryBtn = document.createElement('button');
                        retryBtn.textContent = '重新掃描';
                        retryBtn.className = 'button';
                        retryBtn.onclick = loadNetworks;
                        errorDiv.appendChild(retryBtn);
                        
                        // 添加手動輸入選項
                        var manualDiv = document.createElement('div');
                        manualDiv.style.marginTop = '10px';
                        manualDiv.innerHTML = '<small>或者 <a href="#" onclick="document.getElementById(\'ssid\').focus(); return false;">手動輸入 WiFi 名稱</a></small>';
                        errorDiv.appendChild(manualDiv);
                        
                        networkList.innerHTML = '';
                        networkList.appendChild(errorDiv);
                    });
            }
            
            // 重新掃描函數
            function rescanNetworks() {
                loadNetworks();
            }
            
            // 頁面載入時延遲執行，讓 AP 連接穩定
            document.addEventListener('DOMContentLoaded', function() {
                // 延遲 2 秒再開始掃描，避免影響 AP 穩定性
                setTimeout(loadNetworks, 2000);
            });
            </script>
        )";
    }

    /**
     * 取得倒數計時 JavaScript
     */
    String getCountdownScript(int seconds, const String& targetElementId = "countdown") {
        return R"(
            <script>
            let count = )" + String(seconds) + R"(;
            const countdown = document.getElementById(')" + targetElementId + R"(');
            const timer = setInterval(() => {
                count--;
                if (countdown) countdown.textContent = count;
                if (count <= 0) {
                    clearInterval(timer);
                    if (countdown) countdown.textContent = '處理中...';
                }
            }, 1000);
            </script>
        )";
    }

    /**
     * 取得頁面刷新 JavaScript
     */
    String getRefreshScript(const String& buttonText = "🔄 刷新", const String& functionName = "refreshPage") {
        return R"(
            <script>
            function )" + functionName + R"(() {
                location.reload();
            }
            </script>
        )";
    }

    // ==================== 頁面模板 ====================

    /**
     * 取得錯誤頁面
     */
    String getErrorPage(const String& title, const String& message, const String& backUrl = "/") {
        String html = getPageHeader("錯誤 - " + title);
        html += "<div class=\"container\">";
        html += "<h1>❌ " + title + "</h1>";
        html += "<div class=\"error\">" + message + "</div>";
        html += "<div style=\"text-align: center;\">";
        html += "<a href=\"" + backUrl + "\" class=\"button\">⬅️ 返回</a>";
        html += "</div>";
        html += "</div>";
        html += getPageFooter();
        return html;
    }

    /**
     * 取得成功頁面 (記憶體優化版本)
     */
    String getSuccessPage(const String& title, const String& message, int countdown = 0, const String& redirectUrl = "/") {
        const size_t requestedSize = 3072;
        const size_t bufferSize = WebUIMemory::getAdaptiveBufferSize(requestedSize);
        
        if (!WebUIMemory::canAllocateBuffer(bufferSize)) {
            return "<div class='error'>Insufficient memory for page generation.</div>";
        }
        
        auto buffer = std::make_unique<char[]>(bufferSize);
        if (!buffer) {
            return "<div class='error'>Memory allocation failed.</div>";
        }

        char* p = buffer.get();
        int remaining = bufferSize;
        int written;
        bool overflow = false;

        auto append = [&](const char* format, ...) {
            if (remaining <= 10 || overflow) {
                overflow = true;
                return;
            }
            va_list args;
            va_start(args, format);
            written = vsnprintf(p, remaining, format, args);
            va_end(args);
            if (written > 0 && written < remaining) {
                p += written;
                remaining -= written;
            } else {
                overflow = true;
            }
        };

        // Inline getPageHeader to avoid String concatenation
        append("<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"><title>成功 - %s</title>", title.c_str());
        append("<style>%s</style></head><body>", getCompactCSS());

        // Page content
        append("<div class=\"container\"><h1>✅ %s</h1><div class=\"success\">%s</div>", title.c_str(), message.c_str());

        if (countdown > 0) {
            append("<div style=\"text-align: center; margin: 20px 0;\"><div style=\"font-size: 24px; font-weight: bold; color: #007cba;\"><span id=\"countdown\">%d</span></div><p>秒後自動跳轉...</p></div>", countdown);
            
            // Inline getCountdownScript
            append("<script>let count = %d; const countdown = document.getElementById('countdown'); const timer = setInterval(() => { count--; if (countdown) countdown.textContent = count; if (count <= 0) { clearInterval(timer); if (countdown) countdown.textContent = '處理中...'; } }, 1000);</script>", countdown);

            if (redirectUrl.length() > 0) {
                append("<script>setTimeout(function(){window.location='%s';}, %d);</script>", redirectUrl.c_str(), countdown * 1000);
            }
        }

        append("<div style=\"text-align: center;\"><a href=\"/\" class=\"button secondary\">⬅️ 返回主頁</a></div></div>");
        
        // Inline getPageFooter
        append("</body></html>");

        if (overflow) {
            return "<div style='color:red;'>Error: HTML too large for buffer (" + String(bufferSize) + " bytes)</div>";
        }
        return String(buffer.get());
    }

    /**
     * 取得重啟頁面 (記憶體優化版本)
     */
    String getRestartPage(const String& ip = "") {
        String finalIp = ip.length() > 0 ? ip : WiFi.localIP().toString();
        
        const size_t requestedSize = 2048;
        const size_t bufferSize = WebUIMemory::getAdaptiveBufferSize(requestedSize);
        
        if (!WebUIMemory::canAllocateBuffer(bufferSize)) {
            return "<div class='error'>Insufficient memory for restart page.</div>";
        }
        
        auto buffer = std::make_unique<char[]>(bufferSize);
        if (!buffer) { return "<div class='error'>Memory allocation failed.</div>"; }

        char* p = buffer.get();
        int remaining = bufferSize;
        int written;

        auto append = [&](const char* format, ...) {
            if (remaining <= 1) return;
            va_list args;
            va_start(args, format);
            written = vsnprintf(p, remaining, format, args);
            va_end(args);
            if (written > 0) { p += written; remaining -= written; }
        };

        // Inline getPageHeader
        append("<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>設備重啟中</title>");
        append("<style>%s</style></head><body>", getCompactCSS());
        
        append("<div class=\"container\"><h1>🔄 設備重啟中</h1>");
        append("<div class=\"info\"><p>設備正在重新啟動，請稍候...</p><p>約30秒後可重新訪問設備。</p></div>");
        
        if (finalIp.length() > 0) {
            append("<p>重啟完成後請訪問：<br><a href=\"http://%s\">http://%s</a></p>", finalIp.c_str(), finalIp.c_str());
            append("<script>setTimeout(function(){window.location='http://%s';}, 30000);</script>", finalIp.c_str());
        }
        
        append("</div></body></html>");
        return String(buffer.get());
    }

    // ==================== 完整頁面模板 ====================

    /**
     * 安全的緩衝區append函數 (改進版記憶體優化)
     */
    class SafeHtmlBuilder {
    private:
        std::unique_ptr<char[]> buffer;
        char* p;
        int remaining;
        size_t totalSize;
        bool overflow;
        
    public:
        SafeHtmlBuilder(size_t requestedSize) : overflow(false) {
            // 使用自適應緩衝區大小計算
            totalSize = WebUIMemory::getAdaptiveBufferSize(requestedSize);
            
            // 檢查是否可以分配緩衝區
            if (!WebUIMemory::canAllocateBuffer(totalSize)) {
                overflow = true;
                return;
            }
            
            buffer = std::make_unique<char[]>(totalSize);
            if (!buffer) {
                overflow = true;
                return;
            }
            p = buffer.get();
            remaining = totalSize;
        }
        
        void append(const char* format, ...) {
            if (overflow || remaining <= 10) { // 保留10字節安全邊界
                overflow = true;
                return;
            }
            
            va_list args;
            va_start(args, format);
            int written = vsnprintf(p, remaining, format, args);
            va_end(args);
            
            if (written > 0 && written < remaining) {
                p += written;
                remaining -= written;
            } else {
                overflow = true;
            }
        }
        
        String toString() {
            if (overflow) {
                return "<div style='color:red;'>Error: HTML too large for buffer (" + String(totalSize) + " bytes)</div>";
            }
            return String(buffer.get());
        }
        
        bool isOverflow() { return overflow; }
        int getRemainingSpace() { return remaining; }
    };

    /**
     * 取得簡化的 WiFi 配置頁面（改進記憶體優化版本）
     */
    String getSimpleWiFiConfigPage(const String& saveEndpoint = "/wifi-save", const String& scanEndpoint = "/wifi-scan", 
                                  const String& currentSSID = "", bool showWarning = true) {
        SafeHtmlBuilder html(6144); // 增加到6KB緩衝區

        // --- Header ---
        html.append("<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>WiFi 配置</title>");
        html.append("<style>%s</style></head><body>", getCompactCSS());

        // --- Body ---
        html.append("<div class=\"container\"><h1>📶 WiFi 配置</h1>");
        if (showWarning) {
            html.append("<div class=\"warning\">⚠️ 配置新WiFi後設備將重啟，HomeKit配對狀態會保持。</div>");
        }
        html.append("<h3>可用網路 <button type=\"button\" class=\"button\" onclick=\"rescanNetworks()\">🔄 重新掃描</button></h3>");
        html.append("<div id=\"networks\"><div style=\"padding:15px;text-align:center;color:#666;\">載入中...</div></div>");
        
        // --- Form ---
        html.append("<form action=\"%s\" method=\"POST\">", saveEndpoint.c_str());
        html.append("<div class=\"form-group\"><label for=\"ssid\">WiFi 名稱:</label><input type=\"text\" id=\"ssid\" name=\"ssid\" value=\"%s\" required></div>", currentSSID.c_str());
        html.append("<div class=\"form-group\"><label for=\"password\">WiFi 密碼:</label><input type=\"password\" id=\"password\" name=\"password\"></div>");
        html.append("<button type=\"submit\" class=\"button\">💾 保存並重啟</button></form>");
        
        html.append("<div style=\"text-align:center;margin:20px 0;\"><a href=\"/\" class=\"button secondary\">⬅️ 返回主頁</a></div></div>");

        // --- JavaScript (簡化版) ---
        html.append("<script>");
        html.append("function selectNetwork(ssid){document.getElementById('ssid').value=ssid;}");
        html.append("function loadNetworks(){");
        html.append("fetch('%s').then(r=>r.json()).then(networks=>{", scanEndpoint.c_str());
        html.append("let html='';");
        html.append("networks.forEach(n=>{");
        html.append("html+='<div style=\"padding:8px;border:1px solid #ddd;margin:5px;cursor:pointer\" onclick=\"selectNetwork(\\''+n.ssid+'\\')\">';");
        html.append("html+='<strong>'+n.ssid+'</strong> ('+n.rssi+' dBm) '+(n.secure?'🔒':'🔓');");
        html.append("html+='</div>';");
        html.append("});");
        html.append("document.getElementById('networks').innerHTML=html;");
        html.append("}).catch(()=>{document.getElementById('networks').innerHTML='<p>掃描失敗</p>';});");
        html.append("}");
        html.append("function rescanNetworks(){loadNetworks();}");
        html.append("setTimeout(loadNetworks,2000);");
        html.append("</script>");

        html.append("</body></html>");

        return html.toString();
    }

    /**
     * 取得完整的 WiFi 配置頁面
     */
    String getWiFiConfigPage(const String& saveEndpoint = "/save", const String& scanEndpoint = "/scan", 
                            const String& currentSSID = "", const String& currentDeviceName = "智能恆溫器", 
                            const String& currentPairingCode = "11122333", const String& currentQRID = "HSPN") {
        String html = getPageHeader("WiFi 配置");
        html += "<div class=\"container\">";
        html += "<h1>📡 WiFi 配置</h1>";
        
        html += "<form action=\"" + saveEndpoint + "\" method=\"post\">";
        
        // WiFi 網路列表和配置表單
        html += getWiFiNetworkList("networks");
        html += getWiFiConfigForm(currentSSID);
        
        // HomeKit 配置表單
        html += getHomeKitConfigForm(currentDeviceName, currentPairingCode, currentQRID);
        
        html += "<button type=\"submit\" class=\"button\">💾 保存配置</button>";
        html += "</form>";
        
        // 返回按鈕
        html += "<div style=\"text-align: center; margin-top: 20px;\">";
        html += "<a href=\"/\" class=\"button secondary\">⬅️ 返回主頁</a>";
        html += "</div>";
        
        html += "</div>";
        
        // 添加 WiFi 掃描腳本
        html += getWiFiScanScript(scanEndpoint);
        
        html += getPageFooter();
        return html;
    }

    /**
     * 取得完整的 OTA 頁面 (記憶體優化版本)
     */
    String getOTAPage(const String& deviceIP = "", const String& deviceHostname = "DaiSpan-Thermostat", 
                     const String& otaStatus = "") {
        const size_t requestedSize = 3072;
        const size_t bufferSize = WebUIMemory::getAdaptiveBufferSize(requestedSize);
        
        if (!WebUIMemory::canAllocateBuffer(bufferSize)) {
            return "<div class='error'>Insufficient memory for OTA page.</div>";
        }
        
        auto buffer = std::make_unique<char[]>(bufferSize);
        if (!buffer) { return "<div class='error'>Memory allocation failed.</div>"; }

        char* p = buffer.get();
        int remaining = bufferSize;
        int written;

        auto append = [&](const char* format, ...) {
            if (remaining <= 1) return;
            va_list args;
            va_start(args, format);
            written = vsnprintf(p, remaining, format, args);
            va_end(args);
            if (written > 0) { p += written; remaining -= written; }
        };

        append("<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>OTA 更新</title><style>%s</style></head><body>", getCompactCSS());
        append("<div class=\"container\"><h1>🔄 OTA 遠程更新</h1>");

        if (otaStatus.length() > 0) {
            append("<div class=\"status\"><h3>🔄 OTA 更新狀態</h3>%s</div>", otaStatus.c_str());
        } else {
            append("<div class=\"status\"><h3>🔄 OTA 更新狀態</h3><p><span style=\"color: green;\">●</span> OTA 服務已啟用</p><p><strong>設備主機名:</strong> %s</p><p><strong>IP地址:</strong> %s</p></div>", deviceHostname.c_str(), (deviceIP.length() > 0 ? deviceIP.c_str() : WiFi.localIP().toString().c_str()));
        }

        append("<div class=\"warning\"><h3>⚠️ 注意事項</h3><ul><li>OTA 更新過程中請勿斷電或斷網</li><li>更新失敗可能導致設備無法啟動</li><li>建議在更新前備份當前固件</li><li>更新完成後設備會自動重啟</li></ul></div>");
        append("<div><h3>📝 使用說明</h3><p>使用 PlatformIO 進行 OTA 更新：</p><div class=\"code-block\">pio run -t upload --upload-port %s</div>", (deviceIP.length() > 0 ? deviceIP.c_str() : WiFi.localIP().toString().c_str()));
        append("<p>或使用 Arduino IDE：</p><ol><li>工具 → 端口 → 選擇網路端口</li><li>選擇設備主機名: %s</li><li>輸入 OTA 密碼</li><li>點擊上傳</li></ol></div>", deviceHostname.c_str());
        append("<div style=\"text-align: center; margin-top: 30px;\"><a href=\"/\" class=\"button secondary\">⬅️ 返回主頁</a><a href=\"/restart\" class=\"button danger\">🔄 重新啟動</a></div></div>");
        append("</body></html>");

        return String(buffer.get());
    }

    /**
     * 取得 HomeKit 配置頁面 (改進記憶體優化版本)
     */
    String getHomeKitConfigPage(const String& saveEndpoint = "/homekit-save", 
                               const String& currentPairingCode = "11122333", 
                               const String& currentDeviceName = "智能恆溫器", 
                               const String& currentQRID = "HSPN", 
                               bool homeKitInitialized = false) {
        SafeHtmlBuilder html(5120); // 5KB緩衝區

        html.append("<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>HomeKit 配置</title>");
        html.append("<style>%s</style></head><body>", getCompactCSS());
        html.append("<div class=\"container\"><h1>🏠 HomeKit 配置</h1>");
        
        // 當前配置狀態
        html.append("<div class=\"status\"><h3>📋 當前配置</h3>");
        html.append("<p><strong>配對碼：</strong>%s</p>", currentPairingCode.c_str());
        html.append("<p><strong>設備名稱：</strong>%s</p>", currentDeviceName.c_str());
        html.append("<p><strong>QR ID：</strong>%s</p>", currentQRID.c_str());
        html.append("<p><strong>狀態：</strong>%s</p>", homeKitInitialized ? "✅ 已就緒" : "❌ 未就緒");
        html.append("</div>");
        
        // 警告提示
        html.append("<div class=\"warning\"><h3>⚠️ 重要提醒</h3>");
        html.append("<p>修改配置會中斷現有配對，需要重新配對設備。</p></div>");
        
        // 配置表單
        html.append("<form action=\"%s\" method=\"POST\"><h3>🔧 修改配置</h3>", saveEndpoint.c_str());
        html.append("<div class=\"form-group\">");
        html.append("<label for=\"pairing_code\">配對碼 (8位數字):</label>");
        html.append("<input type=\"text\" id=\"pairing_code\" name=\"pairing_code\" placeholder=\"當前: %s\" pattern=\"[0-9]{8}\" maxlength=\"8\">", currentPairingCode.c_str());
        html.append("</div>");
        
        html.append("<div class=\"form-group\">");
        html.append("<label for=\"device_name\">設備名稱:</label>");
        html.append("<input type=\"text\" id=\"device_name\" name=\"device_name\" placeholder=\"當前: %s\" maxlength=\"50\">", currentDeviceName.c_str());
        html.append("</div>");
        
        html.append("<div class=\"form-group\">");
        html.append("<label for=\"qr_id\">QR識別碼:</label>");
        html.append("<input type=\"text\" id=\"qr_id\" name=\"qr_id\" placeholder=\"當前: %s\" maxlength=\"4\">", currentQRID.c_str());
        html.append("</div>");
        
        html.append("<div style=\"text-align:center;margin:20px 0;\">");
        html.append("<button type=\"submit\" class=\"button\">💾 保存配置</button>");
        html.append("</div></form>");
        
        // 使用說明
        html.append("<div class=\"info\"><h3>💡 配對流程</h3>");
        html.append("<ol><li>修改配置後設備會重啟</li><li>在家庭App中重新添加設備</li><li>使用新的配對碼: <strong>%s</strong></li></ol>", currentPairingCode.c_str());
        html.append("</div>");
        
        html.append("<div style=\"text-align:center;margin:20px 0;\">");
        html.append("<a href=\"/\" class=\"button secondary\">⬅️ 返回主頁</a>");
        html.append("</div></div></body></html>");

        return html.toString();
    }

    /**
     * 取得完整的日誌頁面 (記憶體優化版本)
     */
    String getLogPage(const String& logContent = "", const String& clearEndpoint = "/logs-clear", 
                     const String& apiEndpoint = "/api/logs", int totalEntries = 0, 
                     int infoCount = 0, int warningCount = 0, int errorCount = 0, int shownEntries = 0) {
        const size_t requestedSize = 4096;
        const size_t bufferSize = WebUIMemory::getAdaptiveBufferSize(requestedSize);
        
        if (!WebUIMemory::canAllocateBuffer(bufferSize)) {
            return "<div class='error'>Insufficient memory for log viewer.</div>";
        }
        
        auto buffer = std::make_unique<char[]>(bufferSize);
        if (!buffer) { return "<div class='error'>Memory allocation failed.</div>"; }

        char* p = buffer.get();
        int remaining = bufferSize;
        int written;

        auto append = [&](const char* format, ...) {
            if (remaining <= 1) return;
            va_list args;
            va_start(args, format);
            written = vsnprintf(p, remaining, format, args);
            va_end(args);
            if (written > 0) { p += written; remaining -= written; }
        };

        append("<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>系統日誌</title><style>%s</style></head><body>", getCompactCSS());
        append("<div class=\"container\"><h1>📊 DaiSpan 系統日誌</h1>");
        append("<div style=\"text-align:center;\"><a href=\"%s\" class=\"button\">📋 JSON格式</a><button onclick=\"clearLogs()\" class=\"button danger\">🗑️ 清除日誌</button><a href=\"/\" class=\"button secondary\">⬅️ 返回主頁</a></div>", apiEndpoint.c_str());
        append("<div class=\"status\"><h3>📈 統計資訊</h3><p>總計: %d 條記錄", totalEntries);
        if (shownEntries > 0 && shownEntries < totalEntries) {
            append(" (顯示最新 %d 條)", shownEntries);
        }
        append("</p><p>資訊: %d | 警告: %d | 錯誤: %d</p></div>", infoCount, warningCount, errorCount);
        append("<div class=\"log-container\">%s</div>", logContent.length() > 0 ? logContent.c_str() : "<p style='color:#666;'>沒有可用的日誌記錄</p>");
        append("<p style=\"margin-top:15px;\"><strong>注意：</strong>只顯示最新的記錄。使用 <a href=\"%s\" target=\"_blank\">JSON API</a> 查看完整日誌。</p>", apiEndpoint.c_str());
        append("<script>function clearLogs(){if(confirm('確定要清除所有日誌嗎？')){fetch('%s',{method:'POST'}).then(()=>location.reload());}}</script>", clearEndpoint.c_str());
        append("</div></body></html>");

        return String(buffer.get());
    }

    #ifndef DISABLE_SIMULATION_MODE
    /**
     * 取得模擬控制頁面 (記憶體優化版本)
     */
    String getSimulationControlPage(const String& saveEndpoint = "/simulation-control",
                                   bool power = false, int mode = 0, 
                                   float targetTemp = 22.0, float currentTemp = 25.0, float roomTemp = 25.0,
                                   bool isHeating = false, bool isCooling = false, int fanSpeed = 0) {
        const size_t requestedSize = 6144;
        const size_t bufferSize = WebUIMemory::getAdaptiveBufferSize(requestedSize);
        
        if (!WebUIMemory::canAllocateBuffer(bufferSize)) {
            return "<div class='error'>Insufficient memory for thermostat page.</div>";
        }
        
        auto buffer = std::make_unique<char[]>(bufferSize);
        if (!buffer) { return "<div class='error'>Memory allocation failed.</div>"; }

        char* p = buffer.get();
        int remaining = bufferSize;
        int written;

        bool overflow = false;
        auto append = [&](const char* format, ...) {
            if (remaining <= 10 || overflow) {
                overflow = true;
                return;
            }
            va_list args;
            va_start(args, format);
            written = vsnprintf(p, remaining, format, args);
            va_end(args);
            if (written > 0 && written < remaining) {
                p += written;
                remaining -= written;
            } else {
                overflow = true;
            }
        };

        append("<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>模擬控制</title><style>%s</style></head><body>", getCompactCSS());
        append("<div class=\"container\"><h1>🔧 模擬控制台</h1>");

        const char* modeText = "";
        switch(mode) {
          case 0: modeText = "(關閉)"; break;
          case 1: modeText = "(制熱)"; break;
          case 2: modeText = "(制冷)"; break;
          case 3: modeText = "(自動)"; break;
        }
        const char* runStatus = isHeating ? "🔥 加熱中" : (isCooling ? "❄️ 制冷中" : "⏸️ 待機");
        const char* fanSpeedText = "";
        switch(fanSpeed) {
          case 0: fanSpeedText = "自動"; break;
          case 1: fanSpeedText = "1檔"; break;
          case 2: fanSpeedText = "2檔"; break;
          case 3: fanSpeedText = "3檔"; break;
          case 4: fanSpeedText = "4檔"; break;
          case 5: fanSpeedText = "5檔"; break;
          case 6: fanSpeedText = "安靜"; break;
          default: fanSpeedText = "未知"; break;
        }

        append("<div class=\"status-card\"><h3>📊 當前狀態</h3><p><strong>電源：</strong>%s</p><p><strong>模式：</strong>%d %s</p><p><strong>當前溫度：</strong>%.1f°C</p><p><strong>目標溫度：</strong>%.1f°C</p><p><strong>環境溫度：</strong>%.1f°C</p><p><strong>風量：</strong>%d (%s)</p><p><strong>運行狀態：</strong>%s</p></div>", power ? "開啟" : "關閉", mode, modeText, currentTemp, targetTemp, roomTemp, fanSpeed, fanSpeedText, runStatus);
        append("<div style=\"text-align:center;margin:15px 0;\"><button onclick=\"window.location.reload()\" class=\"button\">🔄 刷新狀態</button></div>");
        append("<div class=\"warning\"><h3>💡 使用說明</h3><ul><li>🔧 這是模擬模式，所有操作都是虛擬的</li><li>📱 HomeKit指令會即時反映在這裡</li><li>🌡️ 溫度會根據運行模式自動變化</li><li>🔄 點擊「刷新狀態」按鈕查看最新狀態</li><li>⚡ 可手動控制電源、模式和溫度參數</li></ul></div>");
        append("<form action=\"%s\" method=\"POST\"><h3>🎛️ 手動控制</h3>", saveEndpoint.c_str());
        append("<div class=\"form-group\"><label for=\"power\">電源控制:</label><select id=\"power\" name=\"power\"><option value=\"1\"%s>開啟</option><option value=\"0\"%s>關閉</option></select></div>", power ? " selected" : "", !power ? " selected" : "");
        append("<div class=\"form-group\"><label for=\"mode\">運行模式:</label><select id=\"mode\" name=\"mode\"><option value=\"0\"%s>關閉</option><option value=\"1\"%s>制熱</option><option value=\"2\"%s>制冷</option><option value=\"3\"%s>自動</option></select></div>", mode == 0 ? " selected" : "", mode == 1 ? " selected" : "", mode == 2 ? " selected" : "", mode == 3 ? " selected" : "");
        append("<div class=\"form-group\"><label for=\"fan_speed\">風量設置:</label><select id=\"fan_speed\" name=\"fan_speed\"><option value=\"0\"%s>自動</option><option value=\"1\"%s>1檔 (低速)</option><option value=\"2\"%s>2檔</option><option value=\"3\"%s>3檔 (中速)</option><option value=\"4\"%s>4檔</option><option value=\"5\"%s>5檔 (高速)</option><option value=\"6\"%s>安靜模式</option></select></div>", fanSpeed == 0 ? " selected" : "", fanSpeed == 1 ? " selected" : "", fanSpeed == 2 ? " selected" : "", fanSpeed == 3 ? " selected" : "", fanSpeed == 4 ? " selected" : "", fanSpeed == 5 ? " selected" : "", fanSpeed == 6 ? " selected" : "");
        append("<div class=\"form-group\"><label for=\"target_temp\">目標溫度 (°C):</label><input type=\"number\" id=\"target_temp\" name=\"target_temp\" min=\"16\" max=\"30\" step=\"0.5\" value=\"%.1f\"></div>", targetTemp);
        append("<div class=\"form-group\"><label for=\"current_temp\">設置當前溫度 (°C):</label><input type=\"number\" id=\"current_temp\" name=\"current_temp\" min=\"10\" max=\"40\" step=\"0.1\" value=\"%.1f\"></div>", currentTemp);
        append("<div class=\"form-group\"><label for=\"room_temp\">設置環境溫度 (°C):</label><input type=\"number\" id=\"room_temp\" name=\"room_temp\" min=\"10\" max=\"40\" step=\"0.1\" value=\"%.1f\"></div>", roomTemp);
        append("<div style=\"text-align:center;margin:20px 0;\"><button type=\"submit\" class=\"button\">🔄 應用設置</button></div></form>");
        append("<div style=\"text-align:center;margin:20px 0;\"><a href=\"/\" class=\"button secondary\">⬅️ 返回主頁</a><a href=\"/simulation-toggle\" class=\"button danger\">🔄 切換到真實模式</a></div></div>");
        append("</body></html>");

        if (overflow) {
            return "<div style='color:red;'>Error: HTML too large for buffer (6144 bytes). Remaining: " + String(remaining) + "</div>";
        }
        return String(buffer.get());
    }

    /**
     * 取得模擬模式切換確認頁面 (記憶體優化版本)
     */
    String getSimulationTogglePage(const String& confirmEndpoint = "/simulation-toggle-confirm",
                                  bool currentMode = false) {
        const size_t requestedSize = 3072;
        const size_t bufferSize = WebUIMemory::getAdaptiveBufferSize(requestedSize);
        
        if (!WebUIMemory::canAllocateBuffer(bufferSize)) {
            return "<div class='error'>Insufficient memory for simulation toggle page.</div>";
        }
        
        auto buffer = std::make_unique<char[]>(bufferSize);
        if (!buffer) { return "<div class='error'>Memory allocation failed.</div>"; }

        char* p = buffer.get();
        int remaining = bufferSize;
        int written;
        bool overflow = false;

        auto append = [&](const char* format, ...) {
            if (remaining <= 10 || overflow) {
                overflow = true;
                return;
            }
            va_list args;
            va_start(args, format);
            written = vsnprintf(p, remaining, format, args);
            va_end(args);
            if (written > 0 && written < remaining) {
                p += written;
                remaining -= written;
            } else {
                overflow = true;
            }
        };

        append("<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>切換運行模式</title><style>%s</style></head><body>", getCompactCSS());
        append("<div class=\"container\"><h1>🔄 切換運行模式</h1>");
        append("<div class=\"warning\"><h3>⚠️ 重要提醒</h3><p>當前模式：%s</p><p>切換模式將會：</p><ul><li>重新啟動設備</li><li>重新初始化控制器</li><li>%s</li></ul></div>", currentMode ? "🔧 模擬模式" : "🏭 真實模式", currentMode ? "啟用真實空調通訊（需要連接S21協議線路）" : "停用真實空調通訊，啟用模擬功能");
        
        const char* targetMode = currentMode ? "真實模式" : "模擬模式";
        const char* targetIcon = currentMode ? "🏭" : "🔧";

        append("<div style=\"text-align:center;margin:20px 0;\"><form action=\"%s\" method=\"POST\" style=\"display:inline;\"><button type=\"submit\" class=\"button danger\">%s 切換到%s</button></form></div>", confirmEndpoint.c_str(), targetIcon, targetMode);
        append("<div style=\"text-align:center;margin:20px 0;\"><a href=\"/\" class=\"button secondary\">⬅️ 返回主頁</a></div></div>");
        append("</body></html>");

        if (overflow) {
            return "<div style='color:red;'>Error: HTML too large for buffer (" + String(bufferSize) + " bytes)</div>";
        }
        return String(buffer.get());
    }
    #endif // DISABLE_SIMULATION_MODE

} // namespace WebUI