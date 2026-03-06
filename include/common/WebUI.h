#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <memory>

namespace WebUI {

    inline const char* getCompactCSS() {
        static const char CSS[] PROGMEM =
            "body{font-family:Arial;margin:10px;background:#f0f0f0}"
            ".container{max-width:600px;margin:0 auto;background:white;padding:15px;border-radius:5px}"
            "h1{color:#333;text-align:center}h2,h3{color:#333}"
            ".button{display:inline-block;padding:8px 15px;margin:5px;background:#007cba;color:white;"
            "text-decoration:none;border-radius:3px;border:none;cursor:pointer}"
            ".button:hover{background:#005a8b}.button.danger{background:#dc3545}.button.secondary{background:#666}"
            ".form-group{margin:10px 0}label{display:block;margin-bottom:3px;font-weight:bold}"
            "input,select{width:100%;padding:8px;border:1px solid #ddd;border-radius:3px;box-sizing:border-box}"
            ".status{background:#e8f4f8;padding:10px;border-radius:3px;margin:10px 0}"
            ".warning{background:#fff3cd;padding:10px;border-radius:3px;margin:10px 0}"
            ".info{background:#d1ecf1;padding:10px;border-radius:3px;margin:10px 0}"
            ".error{background:#f8d7da;color:#721c24;padding:10px;border-radius:3px;margin:10px 0}"
            ".status-card{background:#f8f9fa;border:1px solid #dee2e6;border-radius:5px;padding:15px;margin:15px 0}"
            ".status-item{display:flex;justify-content:space-between;padding:5px 0;border-bottom:1px solid #eee}"
            ".status-item:last-child{border-bottom:none}"
            ".status-label{font-weight:bold;color:#495057}.status-value{color:#6c757d}"
            ".status-good{color:#28a745}.status-warning{color:#ffc107}.status-error{color:#dc3545}";
        return CSS;
    }

    // 通用 snprintf 頁面構建器
    class PageBuilder {
        std::unique_ptr<char[]> buf;
        char* p;
        int rem;
    public:
        PageBuilder(size_t size) : buf(std::make_unique<char[]>(size)), p(buf.get()), rem(size) {}
        void append(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
            if (rem <= 1) return;
            va_list args;
            va_start(args, fmt);
            int n = vsnprintf(p, rem, fmt, args);
            va_end(args);
            if (n > 0 && n < rem) { p += n; rem -= n; }
        }
        String toString() { return String(buf.get()); }
    };

    inline String getSuccessPage(const String& title, const String& message,
                         int countdown = 0, const String& redirectUrl = "/") {
        PageBuilder h(1024);
        h.append("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
                 "<title>%s</title>"
                 "<style>body{font-family:sans-serif;text-align:center;padding:40px;background:#f5f5f5}"
                 ".box{background:#fff;border-radius:10px;padding:30px;max-width:400px;margin:0 auto}"
                 "h1{color:#28a745}a{color:#007cba}</style></head><body>"
                 "<div class='box'><h1>%s</h1><p>%s</p>",
                 title.c_str(), title.c_str(), message.c_str());
        if (countdown > 0) {
            h.append("<p><span id='cd'>%d</span> 秒後自動跳轉...</p>"
                     "<script>let c=%d;const e=document.getElementById('cd');"
                     "setInterval(()=>{c--;if(e)e.textContent=c;},1000);",
                     countdown, countdown);
            if (redirectUrl.length() > 0)
                h.append("setTimeout(()=>location='%s',%d);", redirectUrl.c_str(), countdown * 1000);
            h.append("</script>");
        }
        h.append("<p><a href='/'>返回主頁</a></p></div></body></html>");
        return h.toString();
    }

    inline String getRestartPage(const String& ip = "") {
        String addr = ip.length() > 0 ? ip : WiFi.localIP().toString();
        PageBuilder h(1024);
        h.append("<!DOCTYPE html><html><head><meta charset='UTF-8'><title>重啟中</title>"
                 "<style>%s</style></head><body><div class='container'>"
                 "<h1>設備重啟中</h1>"
                 "<div class='info'><p>請稍候約 30 秒...</p></div>"
                 "<p><a href='http://%s'>http://%s</a></p>"
                 "<script>setTimeout(()=>location='http://%s',30000);</script>"
                 "</div></body></html>",
                 getCompactCSS(), addr.c_str(), addr.c_str(), addr.c_str());
        return h.toString();
    }

    inline String getSimpleWiFiConfigPage(const String& saveEndpoint = "/wifi-save",
                                   const String& scanEndpoint = "/wifi-scan",
                                   const String& currentSSID = "", bool showWarning = true) {
        PageBuilder h(4096);
        h.append("<!DOCTYPE html><html><head><meta charset='UTF-8'><title>WiFi 配置</title>"
                 "<style>%s</style></head><body><div class='container'><h1>WiFi 配置</h1>", getCompactCSS());
        if (showWarning)
            h.append("<div class='warning'>配置新WiFi後設備將重啟。</div>");
        h.append("<h3>可用網路 <button type='button' class='button' onclick='scan()'>重新掃描</button></h3>"
                 "<div id='nets'>載入中...</div>"
                 "<form action='%s' method='POST'>"
                 "<div class='form-group'><label>WiFi 名稱:</label>"
                 "<input type='text' id='ssid' name='ssid' value='%s' required></div>"
                 "<div class='form-group'><label>WiFi 密碼:</label>"
                 "<input type='password' name='password'></div>"
                 "<button type='submit' class='button'>保存並重啟</button></form>"
                 "<div style='text-align:center;margin:20px'><a href='/' class='button secondary'>返回主頁</a></div></div>",
                 saveEndpoint.c_str(), currentSSID.c_str());
        h.append("<script>"
                 "function sel(s){document.getElementById('ssid').value=s;}"
                 "function scan(){"
                 "fetch('%s').then(r=>r.json()).then(ns=>{"
                 "let h='';ns.forEach(n=>{"
                 "h+='<div style=\"padding:8px;border:1px solid #ddd;margin:5px;cursor:pointer\" "
                 "onclick=\"sel(\\''+n.ssid+'\\')\">';"
                 "h+='<b>'+n.ssid+'</b> ('+n.rssi+' dBm)</div>';});"
                 "document.getElementById('nets').innerHTML=h||'<p>未找到網路</p>';"
                 "}).catch(()=>{document.getElementById('nets').innerHTML='<p>掃描失敗</p>';});}"
                 "setTimeout(scan,2000);</script></body></html>", scanEndpoint.c_str());
        return h.toString();
    }

    inline String getOTAPage(const String& deviceIP = "", const String& hostname = "DaiSpan-Thermostat",
                     const String& otaStatus = "") {
        String ip = deviceIP.length() > 0 ? deviceIP : WiFi.localIP().toString();
        PageBuilder h(2048);
        h.append("<!DOCTYPE html><html><head><meta charset='UTF-8'><title>OTA 更新</title>"
                 "<style>%s</style></head><body><div class='container'><h1>OTA 更新</h1>", getCompactCSS());
        h.append("<div class='status'><p>OTA 服務已啟用</p>"
                 "<p><b>主機名:</b> %s</p><p><b>IP:</b> %s</p></div>", hostname.c_str(), ip.c_str());
        h.append("<div class='info'><p>PlatformIO 指令:</p>"
                 "<code>pio run -t upload --upload-port %s</code></div>", ip.c_str());
        h.append("<div class='warning'><p>更新過程中請勿斷電或斷網。</p></div>");
        h.append("<div style='text-align:center;margin:20px'>"
                 "<a href='/' class='button secondary'>返回</a>"
                 "<a href='/restart' class='button danger'>重啟</a></div>"
                 "</div></body></html>");
        return h.toString();
    }

    inline String getHomeKitConfigPage(const String& saveEndpoint = "/homekit-save",
                               const String& pairingCode = "11122333",
                               const String& deviceName = "智能恆溫器",
                               const String& qrID = "HSPN",
                               bool homeKitInit = false) {
        PageBuilder h(3072);
        h.append("<!DOCTYPE html><html><head><meta charset='UTF-8'><title>HomeKit 配置</title>"
                 "<style>%s</style></head><body><div class='container'><h1>HomeKit 配置</h1>", getCompactCSS());
        h.append("<div class='status'><p><b>配對碼:</b> %s</p>"
                 "<p><b>設備名稱:</b> %s</p><p><b>QR ID:</b> %s</p>"
                 "<p><b>狀態:</b> %s</p></div>",
                 pairingCode.c_str(), deviceName.c_str(), qrID.c_str(),
                 homeKitInit ? "已就緒" : "未就緒");
        h.append("<div class='warning'><p>修改配置會中斷現有配對，需要重新配對。</p></div>");
        h.append("<form action='%s' method='POST'>"
                 "<div class='form-group'><label>配對碼 (8位數字):</label>"
                 "<input type='text' name='pairing_code' placeholder='%s' pattern='[0-9]{8}' maxlength='8'></div>"
                 "<div class='form-group'><label>設備名稱:</label>"
                 "<input type='text' name='device_name' placeholder='%s' maxlength='50'></div>"
                 "<div class='form-group'><label>QR識別碼:</label>"
                 "<input type='text' name='qr_id' placeholder='%s' maxlength='4'></div>"
                 "<div style='text-align:center;margin:20px'>"
                 "<button type='submit' class='button'>保存配置</button></div></form>",
                 saveEndpoint.c_str(), pairingCode.c_str(), deviceName.c_str(), qrID.c_str());
        h.append("<div style='text-align:center'><a href='/' class='button secondary'>返回主頁</a></div>"
                 "</div></body></html>");
        return h.toString();
    }

#ifndef DISABLE_SIMULATION_MODE
    inline String getSimulationTogglePage(const String& confirmEndpoint = "/simulation-toggle-confirm",
                                   bool currentMode = false) {
        PageBuilder h(1536);
        h.append("<!DOCTYPE html><html><head><meta charset='UTF-8'><title>切換模式</title>"
                 "<style>%s</style></head><body><div class='container'><h1>切換運行模式</h1>", getCompactCSS());
        h.append("<div class='warning'><p>當前: <b>%s</b></p>"
                 "<p>切換後設備將重啟。</p></div>",
                 currentMode ? "模擬模式" : "真實模式");
        h.append("<div style='text-align:center;margin:20px'>"
                 "<form action='%s' method='POST' style='display:inline'>"
                 "<button type='submit' class='button danger'>切換到%s</button></form></div>",
                 confirmEndpoint.c_str(), currentMode ? "真實模式" : "模擬模式");
        h.append("<div style='text-align:center'><a href='/' class='button secondary'>返回</a></div>"
                 "</div></body></html>");
        return h.toString();
    }
#endif

} // namespace WebUI
