#pragma once

#include <Arduino.h>
#include <WiFi.h>

/**
 * WebUI 共用模組
 * 提供統一的 Web 介面元件，供 WiFiManager 和 main.cpp 使用
 * 避免程式碼重複，統一 UI 風格
 */
namespace WebUI {

    // ==================== CSS 樣式 ====================
    
    /**
     * 取得精簡版 CSS 樣式 (針對記憶體優化)
     */
    String getCommonCSS() {
        return R"(body{font-family:Arial;margin:20px;background:#f0f0f0}.container{max-width:600px;margin:0 auto;background:white;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}h1{color:#333;text-align:center}h2,h3{color:#333}.button{display:inline-block;padding:10px 20px;margin:10px 5px;background:#007cba;color:white;text-decoration:none;border-radius:5px;border:none;cursor:pointer}.button:hover{background:#005a8b}.button.danger{background:#dc3545}.button.danger:hover{background:#c82333}.button.secondary{background:#666}.button.secondary:hover{background:#555})";
    }

    /**
     * 取得表單相關 CSS 樣式
     */
    String getFormCSS() {
        return R"(.form-group{margin:15px 0}label{display:block;margin-bottom:5px;font-weight:bold}input[type=text],input[type=password],input[type=number],select{width:100%;padding:10px;border:1px solid #ddd;border-radius:5px;box-sizing:border-box}input[type=text]:focus,input[type=password]:focus,input[type=number]:focus,select:focus{border-color:#007cba;outline:none}small{color:#666;font-size:12px;display:block;margin-top:5px})";
    }

    /**
     * 取得狀態卡片相關 CSS 樣式
     */
    String getStatusCSS() {
        return R"(
            .status { 
                background: #e8f4f8; 
                padding: 15px; 
                border-radius: 5px; 
                margin: 20px 0; 
            }
            .status-card { 
                background: #f8f9fa; 
                border: 1px solid #dee2e6; 
                padding: 15px; 
                border-radius: 8px; 
                margin: 15px 0; 
            }
            .status-grid { 
                display: grid; 
                grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); 
                gap: 20px; 
                margin: 20px 0; 
            }
            .status-item { 
                display: flex; 
                justify-content: space-between; 
                padding: 5px 0; 
                border-bottom: 1px solid #e9ecef; 
            }
            .status-item:last-child { 
                border-bottom: none; 
            }
            .status-label { 
                font-weight: bold; 
                color: #6c757d; 
            }
            .status-value { 
                color: #212529; 
            }
            .status-good { 
                color: #28a745; 
            }
            .status-warning { 
                color: #ffc107; 
            }
            .status-error { 
                color: #dc3545; 
            }
            .success { 
                background: #d4edda; 
                border: 1px solid #c3e6cb; 
                padding: 15px; 
                border-radius: 5px; 
                margin: 15px 0; 
            }
            .warning { 
                background: #fff3cd; 
                border: 1px solid #ffeaa7; 
                padding: 15px; 
                border-radius: 5px; 
                margin: 15px 0; 
            }
            .error { 
                background: #f8d7da; 
                border: 1px solid #f5c6cb; 
                color: #721c24; 
                padding: 15px; 
                border-radius: 5px; 
                margin: 15px 0; 
            }
            .info { 
                background: #e8f4f8; 
                border: 1px solid #bee5eb; 
                padding: 15px; 
                border-radius: 5px; 
                margin: 15px 0; 
            }
        )";
    }

    /**
     * 取得網路相關 CSS 樣式
     */
    String getNetworkCSS() {
        return R"(
            .network-list { 
                max-height: 200px; 
                overflow-y: auto; 
                border: 1px solid #ddd; 
                border-radius: 5px; 
            }
            .network-item { 
                padding: 10px; 
                border-bottom: 1px solid #eee; 
                cursor: pointer; 
            }
            .network-item:hover { 
                background: #f5f5f5; 
            }
            .network-item:last-child { 
                border-bottom: none; 
            }
            .signal-strength { 
                float: right; 
                color: #666; 
            }
        )";
    }

    /**
     * 取得專用功能 CSS 樣式
     */
    String getSpecialCSS() {
        return R"(
            .code-block { 
                background: #f5f5f5; 
                border: 1px solid #ddd; 
                border-radius: 5px; 
                padding: 15px; 
                margin: 15px 0; 
                font-family: monospace; 
                white-space: pre-wrap; 
            }
            .log-container { 
                background: #000; 
                color: #00ff00; 
                padding: 15px; 
                border-radius: 5px; 
                max-height: 400px; 
                overflow-y: auto; 
                font-size: 12px; 
            }
            .log-entry { 
                margin: 2px 0; 
            }
            .countdown { 
                font-size: 24px; 
                font-weight: bold; 
                color: #007cba; 
            }
            .current-config { 
                background: #f8f9fa; 
                border: 1px solid #dee2e6; 
                padding: 15px; 
                border-radius: 5px; 
                margin: 15px 0; 
            }
        )";
    }

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
        
        // 組合所有 CSS
        header += "<style>";
        header += getCommonCSS();
        header += getFormCSS();
        header += getStatusCSS();
        header += getNetworkCSS();
        header += getSpecialCSS();
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

    // ==================== 系統狀態元件 ====================

    /**
     * 取得系統記憶體資訊
     */
    String getMemoryInfo() {
        uint32_t freeHeap = ESP.getFreeHeap();
        String heapClass = (freeHeap > 100000) ? "status-good" : (freeHeap > 50000) ? "status-warning" : "status-error";
        
        String html = "<div class=\"status-item\">";
        html += "<span class=\"status-label\">可用記憶體:</span>";
        html += "<span class=\"status-value " + heapClass + "\">" + String(freeHeap) + " bytes</span>";
        html += "</div>";
        
        return html;
    }

    /**
     * 取得 WiFi 狀態資訊
     */
    String getWiFiStatus() {
        String html = "";
        
        if (WiFi.status() == WL_CONNECTED) {
            html += "<div class=\"status-item\">";
            html += "<span class=\"status-label\">WiFi SSID:</span>";
            html += "<span class=\"status-value status-good\">" + WiFi.SSID() + "</span>";
            html += "</div>";
            
            html += "<div class=\"status-item\">";
            html += "<span class=\"status-label\">IP地址:</span>";
            html += "<span class=\"status-value\">" + WiFi.localIP().toString() + "</span>";
            html += "</div>";
            
            html += "<div class=\"status-item\">";
            html += "<span class=\"status-label\">MAC地址:</span>";
            html += "<span class=\"status-value\">" + WiFi.macAddress() + "</span>";
            html += "</div>";
            
            html += "<div class=\"status-item\">";
            html += "<span class=\"status-label\">信號強度:</span>";
            int rssi = WiFi.RSSI();
            String rssiClass = (rssi > -50) ? "status-good" : (rssi > -70) ? "status-warning" : "status-error";
            html += "<span class=\"status-value " + rssiClass + "\">" + String(rssi) + " dBm</span>";
            html += "</div>";
            
            html += "<div class=\"status-item\">";
            html += "<span class=\"status-label\">網關:</span>";
            html += "<span class=\"status-value\">" + WiFi.gatewayIP().toString() + "</span>";
            html += "</div>";
        } else {
            html += "<div class=\"status-item\">";
            html += "<span class=\"status-label\">WiFi狀態:</span>";
            html += "<span class=\"status-value status-error\">未連接</span>";
            html += "</div>";
        }
        
        return html;
    }

    /**
     * 取得系統基本資訊
     */
    String getSystemInfo() {
        String html = "";
        
        html += "<div class=\"status-item\">";
        html += "<span class=\"status-label\">晶片型號:</span>";
        html += "<span class=\"status-value\">" + String(ESP.getChipModel()) + "</span>";
        html += "</div>";
        
        html += "<div class=\"status-item\">";
        html += "<span class=\"status-label\">CPU頻率:</span>";
        html += "<span class=\"status-value\">" + String(ESP.getCpuFreqMHz()) + " MHz</span>";
        html += "</div>";
        
        html += "<div class=\"status-item\">";
        html += "<span class=\"status-label\">Flash大小:</span>";
        html += "<span class=\"status-value\">" + String(ESP.getFlashChipSize() / 1024 / 1024) + " MB</span>";
        html += "</div>";
        
        html += "<div class=\"status-item\">";
        html += "<span class=\"status-label\">運行時間:</span>";
        unsigned long uptime = millis();
        unsigned long days = uptime / 86400000;
        unsigned long hours = (uptime % 86400000) / 3600000;
        unsigned long minutes = (uptime % 3600000) / 60000;
        html += "<span class=\"status-value\">" + String(days) + "天 " + String(hours) + "時 " + String(minutes) + "分</span>";
        html += "</div>";
        
        html += "<div class=\"status-item\">";
        html += "<span class=\"status-label\">固件版本:</span>";
        html += "<span class=\"status-value\">v3.0-OTA-FINAL</span>";
        html += "</div>";
        
        return html;
    }

    /**
     * 取得完整系統狀態卡片
     */
    String getSystemStatusCard() {
        String html = "<div class=\"status-card\">";
        html += "<h3>🌐 網路連接</h3>";
        html += getWiFiStatus();
        html += "</div>";
        
        html += "<div class=\"status-card\">";
        html += "<h3>💻 系統資源</h3>";
        html += getMemoryInfo();
        html += getSystemInfo();
        html += "</div>";
        
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
     * 取得成功頁面
     */
    String getSuccessPage(const String& title, const String& message, int countdown = 0, const String& redirectUrl = "/") {
        String html = getPageHeader("成功 - " + title);
        html += "<div class=\"container\">";
        html += "<h1>✅ " + title + "</h1>";
        html += "<div class=\"success\">" + message + "</div>";
        
        if (countdown > 0) {
            html += "<div style=\"text-align: center; margin: 20px 0;\">";
            html += "<div style=\"font-size: 24px; font-weight: bold; color: #007cba;\">";
            html += "<span id=\"countdown\">" + String(countdown) + "</span>";
            html += "</div>";
            html += "<p>秒後自動跳轉...</p>";
            html += "</div>";
            html += getCountdownScript(countdown);
            
            if (redirectUrl.length() > 0) {
                html += "<script>setTimeout(function(){window.location='" + redirectUrl + "';}, " + String(countdown * 1000) + ");</script>";
            }
        }
        
        html += "<div style=\"text-align: center;\">";
        html += "<a href=\"/\" class=\"button\">🏠 返回主頁</a>";
        html += "</div>";
        html += "</div>";
        html += getPageFooter();
        return html;
    }

    /**
     * 取得重啟頁面
     */
    String getRestartPage(const String& ip = "") {
        String finalIp = ip.length() > 0 ? ip : WiFi.localIP().toString();
        
        String html = getPageHeader("設備重啟中");
        html += "<div class=\"container\">";
        html += "<h1>🔄 設備重啟中</h1>";
        html += "<div class=\"info\">";
        html += "<p>設備正在重新啟動，請稍候...</p>";
        html += "<p>約30秒後可重新訪問設備。</p>";
        html += "</div>";
        
        if (finalIp.length() > 0) {
            html += "<p>重啟完成後請訪問：<br>";
            html += "<a href=\"http://" + finalIp + "\">http://" + finalIp + "</a></p>";
            html += "<script>setTimeout(function(){window.location='http://" + finalIp + "';}, 30000);</script>";
        }
        
        html += "</div>";
        html += getPageFooter();
        return html;
    }

    // ==================== 完整頁面模板 ====================

    /**
     * 取得簡化的 WiFi 配置頁面（僅 WiFi 設定，不含 HomeKit）
     */
    String getSimpleWiFiConfigPage(const String& saveEndpoint = "/wifi-save", const String& scanEndpoint = "/wifi-scan", 
                                  const String& currentSSID = "", bool showWarning = true) {
        String html = getPageHeader("WiFi 配置");
        html += "<div class=\"container\">";
        html += "<h1>📶 WiFi 配置</h1>";
        
        if (showWarning) {
            html += "<div class=\"warning\">⚠️ 配置新WiFi後設備將重啟，HomeKit配對狀態會保持。</div>";
        }
        
        // WiFi 網路列表
        html += "<h3>可用網路 <button type=\"button\" class=\"button\" onclick=\"rescanNetworks()\" style=\"font-size:12px;padding:5px 10px;\">🔄 重新掃描</button></h3>";
        html += "<div id=\"networks\">";
        html += "<div style=\"padding:15px;text-align:center;color:#666;\">載入中...</div>";
        html += "</div>";
        
        html += "<form action=\"" + saveEndpoint + "\" method=\"POST\">";
        html += getWiFiConfigForm(currentSSID);
        html += "<button type=\"submit\" class=\"button\">💾 保存WiFi並重啟</button>";
        html += "</form>";
        
        // 返回按鈕
        html += "<div style=\"text-align: center; margin-top: 20px;\">";
        html += "<a href=\"/\" class=\"button secondary\">⬅️ 返回主頁</a>";
        html += "</div>";
        
        html += "</div>";
        
        // 完整的 WiFi 掃描腳本（包含自動載入和手動重掃描）
        html += "<script>";
        html += "function selectNetwork(ssid) {";
        html += "  document.getElementById('ssid').value = ssid;";
        html += "}";
        html += "function loadNetworks() {";
        html += "  const networkList = document.getElementById('networks');";
        html += "  networkList.innerHTML = '<div style=\"text-align: center; padding: 20px;\">正在掃描 WiFi 網路...<br><small>這可能需要幾秒鐘</small></div>';";
        html += "  const controller = new AbortController();";
        html += "  const timeoutId = setTimeout(() => controller.abort(), 15000);";
        html += "  fetch('" + scanEndpoint + "', { signal: controller.signal, cache: 'no-cache' })";
        html += "    .then(response => {";
        html += "      clearTimeout(timeoutId);";
        html += "      if (!response.ok) {";
        html += "        throw new Error('HTTP ' + response.status + ': ' + response.statusText);";
        html += "      }";
        html += "      return response.json();";
        html += "    })";
        html += "    .then(networks => {";
        html += "      networkList.innerHTML = '';";
        html += "      if (networks.length === 0) {";
        html += "        networkList.innerHTML = '<div style=\"text-align: center; padding: 20px; color: orange;\">未找到可用的 WiFi 網路</div>';";
        html += "        return;";
        html += "      }";
        html += "      networks.forEach(network => {";
        html += "        const item = document.createElement('div');";
        html += "        item.className = 'network-item';";
        html += "        item.innerHTML = `<strong>${network.ssid}</strong><span class=\"signal-strength\">${network.rssi} dBm ${network.secure ? '🔒' : '🔓'}</span>`;";
        html += "        item.onclick = () => selectNetwork(network.ssid);";
        html += "        networkList.appendChild(item);";
        html += "      });";
        html += "    })";
        html += "    .catch(error => {";
        html += "      clearTimeout(timeoutId);";
        html += "      console.error('WiFi scan error:', error);";
        html += "      let errorMsg = error.name === 'AbortError' ? '掃描超時' : '載入失敗: ' + error.message;";
        html += "      networkList.innerHTML = '<div style=\"text-align: center; padding: 20px; color: red;\">' + errorMsg + '<br><button class=\"button\" onclick=\"loadNetworks()\">重新掃描</button><br><small style=\"margin-top:10px;display:block;\">或者手動輸入 WiFi 名稱</small></div>';";
        html += "    });";
        html += "}";
        html += "function rescanNetworks() {";
        html += "  loadNetworks();";  // 重用 loadNetworks 函數
        html += "}";
        html += "// 頁面載入時延遲執行，讓 AP 連接穩定";
        html += "document.addEventListener('DOMContentLoaded', function() { setTimeout(loadNetworks, 2000); });";
        html += "</script>";
        
        html += getPageFooter();
        return html;
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
     * 取得完整的 OTA 頁面
     */
    String getOTAPage(const String& deviceIP = "", const String& deviceHostname = "DaiSpan-Thermostat", 
                     const String& otaStatus = "") {
        String html = getPageHeader("OTA 更新");
        html += "<div class=\"container\">";
        html += "<h1>🔄 OTA 遠程更新</h1>";
        
        // OTA 狀態顯示
        if (otaStatus.length() > 0) {
            html += "<div class=\"status\">";
            html += "<h3>🔄 OTA 更新狀態</h3>";
            html += otaStatus;
            html += "</div>";
        } else {
            html += "<div class=\"status\">";
            html += "<h3>🔄 OTA 更新狀態</h3>";
            html += "<p><span style=\"color: green;\">●</span> OTA 服務已啟用</p>";
            html += "<p><strong>設備主機名:</strong> " + deviceHostname + "</p>";
            html += "<p><strong>IP地址:</strong> " + (deviceIP.length() > 0 ? deviceIP : WiFi.localIP().toString()) + "</p>";
            html += "</div>";
        }
        
        html += "<div class=\"warning\">";
        html += "<h3>⚠️ 注意事項</h3>";
        html += "<ul>";
        html += "<li>OTA 更新過程中請勿斷電或斷網</li>";
        html += "<li>更新失敗可能導致設備無法啟動</li>";
        html += "<li>建議在更新前備份當前固件</li>";
        html += "<li>更新完成後設備會自動重啟</li>";
        html += "</ul>";
        html += "</div>";
        
        html += "<div>";
        html += "<h3>📝 使用說明</h3>";
        html += "<p>使用 PlatformIO 進行 OTA 更新：</p>";
        String currentIP = deviceIP.length() > 0 ? deviceIP : WiFi.localIP().toString();
        html += "<div class=\"code-block\">pio run -t upload --upload-port " + currentIP + "</div>";
        
        html += "<p>或使用 Arduino IDE：</p>";
        html += "<ol>";
        html += "<li>工具 → 端口 → 選擇網路端口</li>";
        html += "<li>選擇設備主機名: " + deviceHostname + "</li>";
        html += "<li>輸入 OTA 密碼</li>";
        html += "<li>點擊上傳</li>";
        html += "</ol>";
        html += "</div>";
        
        html += "<div style=\"text-align: center; margin-top: 30px;\">";
        html += "<a href=\"/\" class=\"button\">⬅️ 返回主頁</a>";
        html += "<a href=\"/restart\" class=\"button danger\">🔄 重新啟動</a>";
        html += "</div>";
        
        html += "</div>";
        html += getPageFooter();
        return html;
    }

    /**
     * 取得完整的 HomeKit 配置頁面
     */
    String getHomeKitConfigPage(const String& saveEndpoint = "/homekit-save", 
                               const String& currentPairingCode = "11122333", 
                               const String& currentDeviceName = "智能恆溫器", 
                               const String& currentQRID = "HSPN", 
                               bool homeKitInitialized = false) {
        String html = getPageHeader("HomeKit 配置");
        html += "<div class=\"container\">";
        html += "<h1>🏠 HomeKit 配置</h1>";
        
        // 當前配置顯示
        html += "<div class=\"current-config\">";
        html += "<h3>📋 當前配置</h3>";
        html += "<p><strong>配對碼：</strong>" + currentPairingCode + "</p>";
        html += "<p><strong>設備名稱：</strong>" + currentDeviceName + "</p>";
        html += "<p><strong>QR ID：</strong>" + currentQRID + "</p>";
        html += "<p><strong>HomeKit端口：</strong>1201</p>";
        html += "<p><strong>初始化狀態：</strong>" + String(homeKitInitialized ? "✅ 已就緒" : "❌ 未就緒") + "</p>";
        html += "</div>";
        
        html += "<div class=\"warning\">";
        html += "<h3>⚠️ 重要提醒</h3>";
        html += "<p>修改HomeKit配置會中斷現有配對關係，您需要：</p>";
        html += "<ul>";
        html += "<li>從家庭App中移除現有設備</li>";
        html += "<li>使用新的配對碼重新添加設備</li>";
        html += "<li>重新配置自動化和場景</li>";
        html += "</ul>";
        html += "</div>";
        
        html += "<form action=\"" + saveEndpoint + "\" method=\"POST\">";
        html += "<h3>🔧 修改配置</h3>";
        
        html += "<div class=\"form-group\">";
        html += "<label for=\"pairing_code\">配對碼 (8位數字):</label>";
        html += "<input type=\"text\" id=\"pairing_code\" name=\"pairing_code\" ";
        html += "placeholder=\"留空保持當前: " + currentPairingCode + "\" ";
        html += "pattern=\"[0-9]{8}\" maxlength=\"8\" ";
        html += "title=\"請輸入8位數字作為HomeKit配對碼\">";
        html += "<small>必須是8位純數字，例如：12345678</small>";
        html += "</div>";
        
        html += "<div class=\"form-group\">";
        html += "<label for=\"device_name\">設備名稱:</label>";
        html += "<input type=\"text\" id=\"device_name\" name=\"device_name\" ";
        html += "placeholder=\"留空保持當前: " + currentDeviceName + "\" ";
        html += "maxlength=\"50\">";
        html += "<small>在家庭App中顯示的設備名稱</small>";
        html += "</div>";
        
        html += "<div class=\"form-group\">";
        html += "<label for=\"qr_id\">QR識別碼:</label>";
        html += "<input type=\"text\" id=\"qr_id\" name=\"qr_id\" ";
        html += "placeholder=\"留空保持當前: " + currentQRID + "\" ";
        html += "maxlength=\"4\">";
        html += "<small>QR碼中的設備識別碼，通常為4個字符</small>";
        html += "</div>";
        
        html += "<div style=\"text-align:center;margin:20px 0;\">";
        html += "<button type=\"submit\" class=\"button\">💾 保存HomeKit配置</button>";
        html += "</div>";
        html += "</form>";
        
        html += "<div class=\"info\">";
        html += "<h3>💡 使用說明</h3>";
        html += "<p><strong>配對流程：</strong></p>";
        html += "<ol>";
        html += "<li>修改配置後，設備會自動重啟</li>";
        html += "<li>在家庭App中掃描新的QR碼</li>";
        html += "<li>或手動輸入新的配對碼：<strong>" + currentPairingCode + "</strong></li>";
        html += "<li>完成配對後即可正常使用</li>";
        html += "</ol>";
        html += "</div>";
        
        html += "<div style=\"text-align: center; margin: 20px 0;\">";
        html += "<a href=\"/\" class=\"button secondary\">⬅️ 返回主頁</a>";
        html += "</div>";
        
        html += "</div>";
        html += getPageFooter();
        return html;
    }

    /**
     * 取得完整的日誌頁面
     */
    String getLogPage(const String& logContent = "", const String& clearEndpoint = "/logs-clear", 
                     const String& apiEndpoint = "/api/logs", int totalEntries = 0, 
                     int infoCount = 0, int warningCount = 0, int errorCount = 0, int shownEntries = 0) {
        String html = getPageHeader("系統日誌");
        html += "<div class=\"container\">";
        html += "<h1>📊 DaiSpan 系統日誌</h1>";
        
        html += "<div style=\"text-align:center;\">";
        html += "<a href=\"" + apiEndpoint + "\" class=\"button\">📋 JSON格式</a>";
        html += "<button onclick=\"clearLogs()\" class=\"button danger\">🗑️ 清除日誌</button>";
        html += "<a href=\"/\" class=\"button secondary\">⬅️ 返回</a>";
        html += "</div>";
        
        html += "<div class=\"status\">";
        html += "<h3>📈 統計資訊</h3>";
        html += "<p>總計: " + String(totalEntries) + " 條記錄";
        if (shownEntries > 0 && shownEntries < totalEntries) {
            html += " (顯示最新 " + String(shownEntries) + " 條)";
        }
        html += "</p>";
        html += "<p>資訊: " + String(infoCount) + " | 警告: " + String(warningCount) + " | 錯誤: " + String(errorCount) + "</p>";
        html += "</div>";
        
        // 顯示日誌內容
        html += "<div class=\"log-container\">";
        if (logContent.length() > 0) {
            html += logContent;
        } else {
            html += "<p style=\"color:#666;\">沒有可用的日誌記錄</p>";
        }
        html += "</div>";
        
        html += "<p style=\"margin-top:15px;\"><strong>注意：</strong>只顯示最新的記錄。使用 <a href=\"" + apiEndpoint + "\" target=\"_blank\">JSON API</a> 查看完整日誌。</p>";
        
        html += "<script>";
        html += "function clearLogs(){";
        html += "  if(confirm('確定要清除所有日誌嗎？')){";
        html += "    fetch('" + clearEndpoint + "',{method:'POST'}).then(()=>location.reload());";
        html += "  }";
        html += "}";
        html += "</script>";
        
        html += "</div>";
        html += getPageFooter();
        return html;
    }

    /**
     * 取得模擬控制頁面
     */
    String getSimulationControlPage(const String& saveEndpoint = "/simulation-control",
                                   bool power = false, int mode = 0, 
                                   float targetTemp = 22.0, float currentTemp = 25.0, float roomTemp = 25.0,
                                   bool isHeating = false, bool isCooling = false) {
        String html = getPageHeader("模擬控制");
        html += "<div class=\"container\">";
        html += "<h1>🔧 模擬控制台</h1>";
        
        // 當前狀態顯示
        html += "<div class=\"status-card\">";
        html += "<h3>📊 當前狀態</h3>";
        html += "<p><strong>電源：</strong>" + String(power ? "開啟" : "關閉") + "</p>";
        html += "<p><strong>模式：</strong>" + String(mode) + " ";
        switch(mode) {
          case 0: html += "(關閉)"; break;
          case 1: html += "(制熱)"; break;
          case 2: html += "(制冷)"; break;
          case 3: html += "(自動)"; break;
        }
        html += "</p>";
        html += "<p><strong>當前溫度：</strong>" + String(currentTemp, 1) + "°C</p>";
        html += "<p><strong>目標溫度：</strong>" + String(targetTemp, 1) + "°C</p>";
        html += "<p><strong>環境溫度：</strong>" + String(roomTemp, 1) + "°C</p>";
        html += "<p><strong>運行狀態：</strong>";
        if (isHeating) {
          html += "🔥 加熱中";
        } else if (isCooling) {
          html += "❄️ 制冷中";
        } else {
          html += "⏸️ 待機";
        }
        html += "</p>";
        html += "</div>";
        
        html += "<div style=\"text-align:center;margin:15px 0;\">";
        html += "<button onclick=\"window.location.reload()\" class=\"button\">🔄 刷新狀態</button>";
        html += "</div>";
        
        html += "<div class=\"warning\">";
        html += "<h3>💡 使用說明</h3>";
        html += "<p><strong>模擬邏輯：</strong></p>";
        html += "<ul>";
        html += "<li>🔧 這是模擬模式，所有操作都是虛擬的</li>";
        html += "<li>📱 HomeKit指令會即時反映在這裡</li>";
        html += "<li>🌡️ 溫度會根據運行模式自動變化</li>";
        html += "<li>🔄 點擊「刷新狀態」按鈕查看最新狀態</li>";
        html += "<li>⚡ 可手動控制電源、模式和溫度參數</li>";
        html += "</ul>";
        html += "</div>";
        
        // 手動控制表單
        html += "<form action=\"" + saveEndpoint + "\" method=\"POST\">";
        html += "<h3>🎛️ 手動控制</h3>";
        
        html += "<div class=\"form-group\">";
        html += "<label for=\"power\">電源控制:</label>";
        html += "<select id=\"power\" name=\"power\">";
        html += "<option value=\"1\"" + String(power ? " selected" : "") + ">開啟</option>";
        html += "<option value=\"0\"" + String(!power ? " selected" : "") + ">關閉</option>";
        html += "</select>";
        html += "</div>";
        
        html += "<div class=\"form-group\">";
        html += "<label for=\"mode\">運行模式:</label>";
        html += "<select id=\"mode\" name=\"mode\">";
        html += "<option value=\"0\"" + String(mode == 0 ? " selected" : "") + ">關閉</option>";
        html += "<option value=\"1\"" + String(mode == 1 ? " selected" : "") + ">制熱</option>";
        html += "<option value=\"2\"" + String(mode == 2 ? " selected" : "") + ">制冷</option>";
        html += "<option value=\"3\"" + String(mode == 3 ? " selected" : "") + ">自動</option>";
        html += "</select>";
        html += "</div>";
        
        html += "<div class=\"form-group\">";
        html += "<label for=\"target_temp\">目標溫度 (°C):</label>";
        html += "<input type=\"number\" id=\"target_temp\" name=\"target_temp\" ";
        html += "min=\"16\" max=\"30\" step=\"0.5\" ";
        html += "value=\"" + String(targetTemp, 1) + "\">";
        html += "</div>";
        
        html += "<div class=\"form-group\">";
        html += "<label for=\"current_temp\">設置當前溫度 (°C):</label>";
        html += "<input type=\"number\" id=\"current_temp\" name=\"current_temp\" ";
        html += "min=\"10\" max=\"40\" step=\"0.1\" ";
        html += "value=\"" + String(currentTemp, 1) + "\">";
        html += "</div>";
        
        html += "<div class=\"form-group\">";
        html += "<label for=\"room_temp\">設置環境溫度 (°C):</label>";
        html += "<input type=\"number\" id=\"room_temp\" name=\"room_temp\" ";
        html += "min=\"10\" max=\"40\" step=\"0.1\" ";
        html += "value=\"" + String(roomTemp, 1) + "\">";
        html += "</div>";
        
        html += "<div style=\"text-align:center;margin:20px 0;\">";
        html += "<button type=\"submit\" class=\"button\">🔄 應用設置</button>";
        html += "</div>";
        html += "</form>";
        
        html += "<div style=\"text-align:center;margin:20px 0;\">";
        html += "<a href=\"/\" style=\"color:#007cba;text-decoration:none;\">⬅️ 返回主頁</a> | ";
        html += "<a href=\"/simulation-toggle\" style=\"color:#dc3545;text-decoration:none;\">🔄 切換到真實模式</a>";
        html += "</div>";
        
        html += "</div>";
        html += getPageFooter();
        return html;
    }

    /**
     * 取得模擬模式切換確認頁面
     */
    String getSimulationTogglePage(const String& confirmEndpoint = "/simulation-toggle-confirm",
                                  bool currentMode = false) {
        String html = getPageHeader("切換運行模式");
        html += "<div class=\"container\">";
        html += "<h1>🔄 切換運行模式</h1>";
        
        html += "<div class=\"warning\">";
        html += "<h3>⚠️ 重要提醒</h3>";
        html += "<p>當前模式：" + String(currentMode ? "🔧 模擬模式" : "🏭 真實模式") + "</p>";
        html += "<p>切換模式將會：</p>";
        html += "<ul>";
        html += "<li>重新啟動設備</li>";
        html += "<li>重新初始化控制器</li>";
        if (currentMode) {
          html += "<li>啟用真實空調通訊（需要連接S21協議線路）</li>";
        } else {
          html += "<li>停用真實空調通訊，啟用模擬功能</li>";
        }
        html += "</ul>";
        html += "</div>";
        
        String targetMode = currentMode ? "真實模式" : "模擬模式";
        String targetIcon = currentMode ? "🏭" : "🔧";
        
        html += "<div style=\"text-align:center;margin:20px 0;\">";
        html += "<form action=\"" + confirmEndpoint + "\" method=\"POST\" style=\"display:inline;\">";
        html += "<button type=\"submit\" class=\"button danger\">" + targetIcon + " 切換到" + targetMode + "</button>";
        html += "</form>";
        html += "</div>";
        
        html += "<div style=\"text-align:center;margin:20px 0;\">";
        html += "<a href=\"/\" style=\"color:#007cba;text-decoration:none;\">⬅️ 取消並返回主頁</a>";
        html += "</div>";
        
        html += "</div>";
        html += getPageFooter();
        return html;
    }

} // namespace WebUI