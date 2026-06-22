#include "web/web_server.h"
#include "config/config_manager.h"
#include "controller/thermostat_ctrl.h"
#include "s21/s21_protocol.h"
#include "wifi/wifi_manager.h"
#include "common/debug.h"
#include <esp_http_server.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static const char *TAG = LOG_TAG_WEB;
static httpd_handle_t server = NULL;
static bool server_ap_mode = false;

/* Uptime from a 64-bit microsecond source so it never wraps at ~49.7 days like
 * the raw 32-bit tick count would. */
static uint32_t uptime_seconds(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000LL);
}

/* Constant-time string compare to avoid leaking the key via response timing. */
static bool ct_equal(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    size_t n = la > lb ? la : lb;
    unsigned char diff = (unsigned char)(la ^ lb);
    for (size_t i = 0; i < n; i++) {
        char ca = i < la ? a[i] : 0;
        char cb = i < lb ? b[i] : 0;
        diff |= (unsigned char)(ca ^ cb);
    }
    return diff == 0;
}

/* Authorize a firmware/config-mutating request via the ?key=... query param,
 * compared against the stored OTA key. A hostile LAN node must not be able to
 * flash firmware or rewrite credentials. */
static bool check_auth(httpd_req_t *req)
{
    char query[160];
    char key[33] = {0};
    char expected[33] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) return false;
    if (httpd_query_key_value(query, "key", key, sizeof(key)) != ESP_OK) return false;
    config_get_ota_key(expected, sizeof(expected));
    return ct_equal(key, expected);
}

/* HTML-escape src into dst for safe interpolation into an attribute/body. */
static void html_escape(char *dst, size_t dst_size, const char *src)
{
    size_t di = 0;
    for (; *src; src++) {
        const char *rep = NULL;
        switch (*src) {
        case '&':  rep = "&amp;";  break;
        case '<':  rep = "&lt;";   break;
        case '>':  rep = "&gt;";   break;
        case '"':  rep = "&quot;"; break;
        case '\'': rep = "&#39;";  break;
        default: break;
        }
        if (rep) {
            size_t rl = strlen(rep);
            if (di + rl >= dst_size) break;
            memcpy(dst + di, rep, rl);
            di += rl;
        } else {
            if (di + 1 >= dst_size) break;
            dst[di++] = *src;
        }
    }
    dst[di] = '\0';
}

static esp_err_t handler_root_ap(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<title>DaiSpan Setup</title>"
        "<style>body{font-family:sans-serif;max-width:400px;margin:0 auto;padding:20px}"
        ".btn{display:inline-block;padding:12px 24px;background:#007aff;color:#fff;"
        "text-decoration:none;border-radius:6px;font-size:16px}</style></head>"
        "<body><h1>DaiSpan Setup</h1>"
        "<p>Connect to your WiFi network to get started.</p>"
        "<a class='btn' href='/wifi'>Configure WiFi</a>"
        "</body></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handler_root(httpd_req_t *req)
{
    char buf[3072];
    uint32_t uptime = uptime_seconds();
    uint32_t hr = uptime / 3600;
    uint32_t min = (uptime % 3600) / 60;
    uint32_t sec = uptime % 60;

    bool power = thermostat_ctrl_get_power();
    uint8_t mode = thermostat_ctrl_get_ac_mode();
    uint8_t fan = thermostat_ctrl_get_fan_speed();
    bool swing_v = thermostat_ctrl_get_swing(SWING_VERTICAL);

    int len = snprintf(buf, sizeof(buf),
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta http-equiv='refresh' content='30'>"
        "<title>DaiSpan</title>"
        "<style>body{font-family:sans-serif;max-width:600px;margin:0 auto;padding:20px}"
        ".card{background:#f5f5f5;border-radius:8px;padding:15px;margin:10px 0}"
        ".btn{display:inline-block;padding:10px 20px;background:#007aff;color:#fff;"
        "text-decoration:none;border-radius:6px;margin:5px}"
        "select,input[type=number]{padding:8px;border:1px solid #ccc;border-radius:4px;margin:4px 0}"
        ".ctrl{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin:8px 0}"
        ".ctrl label{min-width:60px}"
        "button{padding:10px 20px;background:#007aff;color:#fff;border:none;border-radius:6px}"
        "button.off{background:#999}</style></head><body>"
        "<h1>DaiSpan v2</h1>"
        "<div class='card'>"
        "<p>Memory: %lu bytes free</p>"
        "<p>Uptime: %lu:%02lu:%02lu</p>"
        "<p>WiFi: %s (%d dBm)</p>"
        "<p>Power: %s | Temp: %.1f / %.1f C</p>"
        "<p>Controller: %s (errors: %lu)</p>"
        "</div>"
        "<div class='card'><h3>Control</h3>"
        "<form method='POST' action='/control'>"
        "<div class='ctrl'><label>Power</label>"
        "<button name='power' value='on'%s>ON</button>"
        "<button name='power' value='off' class='off'%s>OFF</button></div>"
        "<div class='ctrl'><label>Mode</label>"
        "<select name='mode'>"
        "<option value='1'%s>Auto</option>"
        "<option value='2'%s>Dry</option>"
        "<option value='3'%s>Cool</option>"
        "<option value='4'%s>Heat</option>"
        "<option value='6'%s>Fan</option>"
        "</select></div>"
        "<div class='ctrl'><label>Temp</label>"
        "<input type='number' name='temp' min='16' max='30' step='0.5' value='%.1f'></div>"
        "<div class='ctrl'><label>Fan</label>"
        "<select name='fan'>"
        "<option value='0'%s>Auto</option>"
        "<option value='1'%s>Quiet</option>"
        "<option value='3'%s>1</option>"
        "<option value='4'%s>2</option>"
        "<option value='5'%s>3</option>"
        "<option value='6'%s>4</option>"
        "<option value='7'%s>5</option>"
        "</select></div>"
        "<div class='ctrl'><label>Swing V</label>"
        "<input type='checkbox' name='swing_v' value='1'%s></div>"
        "<div class='ctrl'><button type='submit'>Apply</button></div>"
        "</form></div>"
        "<div><a class='btn' href='/wifi'>WiFi</a>"
        "<a class='btn' href='/settings'>Settings</a>"
        "<a class='btn' href='/ota'>OTA</a>"
        "<a class='btn' href='/api/health'>Health</a>"
        "<a class='btn' href='/api/metrics'>Metrics</a></div>"
        "</body></html>",
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long)hr, (unsigned long)min, (unsigned long)sec,
        wifi_manager_get_ip(), wifi_manager_get_rssi(),
        power ? "ON" : "OFF",
        thermostat_ctrl_get_current_temp(),
        thermostat_ctrl_get_target_temp(),
        thermostat_ctrl_is_healthy() ? "Healthy" : "Error",
        (unsigned long)thermostat_ctrl_get_error_count(),
        power ? " style='font-weight:bold'" : "",
        !power ? " style='font-weight:bold'" : "",
        (mode == AC_MODE_AUTO || mode == AC_MODE_AUTO_2 || mode == AC_MODE_AUTO_3) ? " selected" : "",
        mode == AC_MODE_DRY  ? " selected" : "",
        mode == AC_MODE_COOL ? " selected" : "",
        mode == AC_MODE_HEAT ? " selected" : "",
        mode == AC_MODE_FAN  ? " selected" : "",
        thermostat_ctrl_get_target_temp(),
        fan == 0 ? " selected" : "",
        fan == 1 ? " selected" : "",
        fan == 3 ? " selected" : "",
        fan == 4 ? " selected" : "",
        fan == 5 ? " selected" : "",
        fan == 6 ? " selected" : "",
        fan == 7 ? " selected" : "",
        swing_v ? " checked" : ""
    );

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, buf, len);
}

static esp_err_t handler_wifi(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<title>WiFi Config</title>"
        "<style>body{font-family:sans-serif;max-width:400px;margin:0 auto;padding:20px}"
        "input{width:100%%;padding:10px;margin:5px 0;box-sizing:border-box;"
        "border:1px solid #ccc;border-radius:4px}"
        "button{width:100%%;padding:12px;background:#007aff;color:#fff;"
        "border:none;border-radius:6px;font-size:16px}</style></head>"
        "<body><h1>WiFi Config</h1>"
        "<form id='f' method='POST' action='/wifi-save'>"
        "<label>SSID</label><input name='ssid' required>"
        "<label>Password</label><input name='password' type='password'>"
        "<label>Auth Key (blank in setup mode)</label><input id='k' type='password'>"
        "<br><button type='submit'>Save &amp; Restart</button>"
        "</form>"
        "<script>document.getElementById('f').onsubmit=function(){"
        "var k=document.getElementById('k').value;"
        "if(k)this.action='/wifi-save?key='+encodeURIComponent(k)}</script>"
        "</body></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

/* URL-decode in place: %XX -> char, '+' -> ' ' */
static void url_decode(char *dst, const char *src, size_t dst_size)
{
    size_t di = 0;
    while (*src && di < dst_size - 1) {
        if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            char hex[3] = { src[1], src[2], 0 };
            dst[di++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            dst[di++] = ' ';
            src++;
        } else {
            dst[di++] = *src++;
        }
    }
    dst[di] = '\0';
}

static esp_err_t handler_wifi_save(httpd_req_t *req)
{
    /* AP/setup mode has no key yet (first-time config), so only gate in STA. */
    if (!server_ap_mode && !check_auth(req)) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid auth key");
    }

    char content[256];
    int len = httpd_req_recv(req, content, sizeof(content) - 1);
    if (len <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
    }
    content[len] = '\0';

    /* Parse URL-encoded form data */
    char raw[65] = {0};
    char ssid[33] = {0};
    char pass[65] = {0};

    char *p = strstr(content, "ssid=");
    if (p) {
        p += 5;
        char *end = strchr(p, '&');
        size_t slen = end ? (size_t)(end - p) : strlen(p);
        if (slen >= sizeof(raw)) slen = sizeof(raw) - 1;
        memcpy(raw, p, slen);
        raw[slen] = '\0';
        url_decode(ssid, raw, sizeof(ssid));
    }

    p = strstr(content, "password=");
    if (p) {
        p += 9;
        char *end = strchr(p, '&');
        size_t slen = end ? (size_t)(end - p) : strlen(p);
        if (slen >= sizeof(raw)) slen = sizeof(raw) - 1;
        memcpy(raw, p, slen);
        raw[slen] = '\0';
        url_decode(pass, raw, sizeof(pass));
    }

    if (strlen(ssid) == 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
    }

    ESP_LOGI(TAG, "WiFi config: SSID='%s' pass_len=%d", ssid, (int)strlen(pass));
    config_set_wifi_credentials(ssid, pass);

    const char *html =
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta http-equiv='refresh' content='3;url=/'>"
        "<title>Saved</title></head><body>"
        "<h1>WiFi config saved!</h1><p>Restarting...</p></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);

    /* Restart after response is sent */
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;  /* unreachable */
}

static esp_err_t handler_api_health(httpd_req_t *req)
{
    char buf[128];
    int len = snprintf(buf, sizeof(buf),
        "{\"status\":\"ok\",\"freeHeap\":%lu,\"uptime\":%lu}",
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long)uptime_seconds());
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, len);
}

static esp_err_t handler_api_metrics(httpd_req_t *req)
{
    char buf[512];
    int len = snprintf(buf, sizeof(buf),
        "{"
        "\"freeHeap\":%lu,"
        "\"minFreeHeap\":%lu,"
        "\"uptime\":%lu,"
        "\"rssi\":%d,"
        "\"ip\":\"%s\","
        "\"power\":%s,"
        "\"mode\":%d,"
        "\"targetTemp\":%.1f,"
        "\"currentTemp\":%.1f,"
        "\"fanSpeed\":%d,"
        "\"swingV\":%s,"
        "\"healthy\":%s,"
        "\"errors\":%lu"
        "}",
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long)esp_get_minimum_free_heap_size(),
        (unsigned long)uptime_seconds(),
        wifi_manager_get_rssi(),
        wifi_manager_get_ip(),
        thermostat_ctrl_get_power() ? "true" : "false",
        thermostat_ctrl_get_mode(),
        thermostat_ctrl_get_target_temp(),
        thermostat_ctrl_get_current_temp(),
        thermostat_ctrl_get_fan_speed(),
        thermostat_ctrl_get_swing(SWING_VERTICAL) ? "true" : "false",
        thermostat_ctrl_is_healthy() ? "true" : "false",
        (unsigned long)thermostat_ctrl_get_error_count()
    );
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, len);
}

static esp_err_t handler_ota_page(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<title>OTA Update</title>"
        "<style>body{font-family:sans-serif;max-width:400px;margin:0 auto;padding:20px}"
        "input[type=file]{margin:10px 0}"
        "button{width:100%%;padding:12px;background:#007aff;color:#fff;"
        "border:none;border-radius:6px;font-size:16px}"
        "#progress{display:none;margin:10px 0}"
        "</style></head><body><h1>OTA Update</h1>"
        "<form id='f' method='POST' action='/ota' enctype='multipart/form-data'>"
        "<input type='file' name='firmware' accept='.bin' required>"
        "<input id='k' type='password' placeholder='Auth key' required>"
        "<br><button type='submit'>Upload</button>"
        "</form>"
        "<div id='progress'><p>Uploading... please wait</p></div>"
        "<script>document.getElementById('f').onsubmit=function(){"
        "this.action='/ota?key='+encodeURIComponent(document.getElementById('k').value);"
        "document.getElementById('progress').style.display='block'}</script>"
        "</body></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handler_ota_upload(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid auth key");
    }

    esp_ota_handle_t ota_handle = 0;
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);

    if (!update_partition) {
        ESP_LOGE(TAG, "OTA: no update partition found");
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
    }

    ESP_LOGI(TAG, "OTA: writing to partition '%s' at 0x%lx, size %lu",
             update_partition->label,
             (unsigned long)update_partition->address,
             (unsigned long)update_partition->size);

    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
    }

    char buf[1024];
    int remaining = req->content_len;
    bool header_skipped = false;
    int sep_match = 0;              /* progress matching "\r\n\r\n" across chunks */
    int total_written = 0;
    static const char SEP[4] = { '\r', '\n', '\r', '\n' };

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, sizeof(buf) < (size_t)remaining ? sizeof(buf) : (size_t)remaining);
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ESP_LOGE(TAG, "OTA: recv error");
            esp_ota_abort(ota_handle);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
        }

        char *data = buf;
        int data_len = recv_len;

        /* Skip the multipart header, matching the \r\n\r\n separator with state
         * carried across chunks so a separator split between two recv() calls is
         * handled (the old single-buffer scan discarded the whole chunk and then
         * every following chunk, breaking the upload). */
        if (!header_skipped) {
            int i = 0;
            for (; i < recv_len; i++) {
                if (buf[i] == SEP[sep_match]) {
                    if (++sep_match == 4) { i++; header_skipped = true; break; }
                } else {
                    sep_match = (buf[i] == '\r') ? 1 : 0;
                }
            }
            if (!header_skipped) {
                remaining -= recv_len;  /* whole chunk was header; keep sep_match */
                continue;
            }
            data = &buf[i];
            data_len = recv_len - i;
        }

        if (data_len > 0) {
            err = esp_ota_write(ota_handle, data, data_len);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "OTA write failed: %s", esp_err_to_name(err));
                esp_ota_abort(ota_handle);
                return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
            }
            total_written += data_len;
        }
        remaining -= recv_len;
    }

    ESP_LOGI(TAG, "OTA: %d bytes written, finalizing...", total_written);

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA end failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Image validation failed");
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA set boot failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
    }

    const char *html =
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<title>OTA OK</title></head><body>"
        "<h1>Update successful!</h1><p>Restarting...</p></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "OTA complete, restarting...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

static esp_err_t handler_api_debug(httpd_req_t *req)
{
    uint8_t g1[8] = {0};
    int g1_len = s21_get_last_g1_raw(g1, sizeof(g1));

    char buf[256];
    int len = snprintf(buf, sizeof(buf),
        "{\"g1_len\":%d,"
        "\"g1_hex\":\"0x%02X 0x%02X 0x%02X 0x%02X\","
        "\"targetTemp\":%.1f,"
        "\"swingV\":%s,"
        "\"swingH\":%s}",
        g1_len,
        g1[0], g1[1], g1[2], g1[3],
        thermostat_ctrl_get_target_temp(),
        thermostat_ctrl_get_swing(SWING_VERTICAL) ? "true" : "false",
        thermostat_ctrl_get_swing(SWING_HORIZONTAL) ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, len);
}

static esp_err_t handler_settings(httpd_req_t *req)
{
    char name[33] = {0};
    char code[12] = {0};
    config_get_hk_name(name, sizeof(name));
    config_get_hk_setup_code(code, sizeof(code));

    /* Escape persisted values before reflecting them into HTML attributes; the
     * device name is attacker-controllable via /settings-save (stored XSS). */
    char name_esc[200] = {0};
    char code_esc[80] = {0};
    html_escape(name_esc, sizeof(name_esc), name);
    html_escape(code_esc, sizeof(code_esc), code);

    char buf[1536];
    int len = snprintf(buf, sizeof(buf),
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<title>Settings</title>"
        "<style>body{font-family:sans-serif;max-width:400px;margin:0 auto;padding:20px}"
        "input{width:100%%;padding:10px;margin:5px 0;box-sizing:border-box;"
        "border:1px solid #ccc;border-radius:4px}"
        "button{width:100%%;padding:12px;background:#007aff;color:#fff;"
        "border:none;border-radius:6px;font-size:16px;margin:10px 0}"
        ".btn{display:inline-block;padding:10px 20px;background:#007aff;color:#fff;"
        "text-decoration:none;border-radius:6px}</style></head>"
        "<body><h1>Settings</h1>"
        "<form id='f' method='POST' action='/settings-save'>"
        "<label>Device Name</label>"
        "<input name='name' value='%s' maxlength='32' required>"
        "<label>HomeKit Pairing Code</label>"
        "<input name='code' value='%s' maxlength='10' placeholder='XXX-XX-XXX' required>"
        "<label>Auth Key</label>"
        "<input id='k' type='password' placeholder='Auth key' required>"
        "<button type='submit'>Save</button>"
        "</form>"
        "<script>document.getElementById('f').onsubmit=function(){"
        "this.action='/settings-save?key='+encodeURIComponent(document.getElementById('k').value)}</script>"
        "<a class='btn' href='/'>Back</a>"
        "</body></html>",
        name_esc, code_esc
    );

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, buf, len);
}

/* Captive portal handler: redirect all unknown URLs to /wifi */
static esp_err_t handler_captive_redirect(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/wifi");
    return httpd_resp_send(req, NULL, 0);
}

/* Apple captive portal detection: return success so iPhone stays connected */
static esp_err_t handler_hotspot_detect(httpd_req_t *req)
{
    const char *html = "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handler_control(httpd_req_t *req)
{
    char content[256];
    int len = httpd_req_recv(req, content, sizeof(content) - 1);
    if (len <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
    }
    content[len] = '\0';

    /* Power: form has two submit buttons, only clicked one is sent */
    char *p = strstr(content, "power=");
    if (p) {
        bool on = (strncmp(p + 6, "on", 2) == 0);
        thermostat_ctrl_set_power(on);
    }

    /* Mode (AC mode values: 1=auto, 2=dry, 3=cool, 4=heat, 6=fan) */
    p = strstr(content, "mode=");
    if (p) {
        int mode = atoi(p + 5);
        if (mode >= 0 && mode <= 7) {
            thermostat_ctrl_set_ac_mode((uint8_t)mode);
        }
    }

    /* Temperature */
    p = strstr(content, "temp=");
    if (p) {
        float temp = strtof(p + 5, NULL);
        if (temp >= 16.0f && temp <= 30.0f) {
            thermostat_ctrl_set_temperature(temp);
        }
    }

    /* Fan speed */
    p = strstr(content, "fan=");
    if (p) {
        int fan = atoi(p + 4);
        if (fan >= 0 && fan <= 7) {
            thermostat_ctrl_set_fan_speed((uint8_t)fan);
        }
    }

    /* Swing vertical: checkbox - present means on, absent means off */
    p = strstr(content, "swing_v=");
    thermostat_ctrl_set_swing(SWING_VERTICAL, p != NULL);

    /* 302 redirect back to root */
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t handler_settings_save(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid auth key");
    }

    char content[256];
    int len = httpd_req_recv(req, content, sizeof(content) - 1);
    if (len <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
    }
    content[len] = '\0';

    char raw[65] = {0};
    char name[33] = {0};
    char code[12] = {0};

    char *p = strstr(content, "name=");
    if (p) {
        p += 5;
        char *end = strchr(p, '&');
        size_t slen = end ? (size_t)(end - p) : strlen(p);
        if (slen >= sizeof(raw)) slen = sizeof(raw) - 1;
        memcpy(raw, p, slen);
        raw[slen] = '\0';
        url_decode(name, raw, sizeof(name));
    }

    p = strstr(content, "code=");
    if (p) {
        p += 5;
        char *end = strchr(p, '&');
        size_t slen = end ? (size_t)(end - p) : strlen(p);
        if (slen >= sizeof(raw)) slen = sizeof(raw) - 1;
        memcpy(raw, p, slen);
        raw[slen] = '\0';
        url_decode(code, raw, sizeof(code));
    }

    if (strlen(name) > 0) config_set_hk_name(name);
    if (strlen(code) > 0) config_set_hk_setup_code(code);

    ESP_LOGI(TAG, "Settings saved: name='%s' code='%s'", name, code);

    const char *html =
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<title>Saved</title>"
        "<style>body{font-family:sans-serif;max-width:400px;margin:0 auto;padding:20px}"
        ".btn{display:inline-block;padding:10px 20px;background:#007aff;color:#fff;"
        "text-decoration:none;border-radius:6px}</style></head>"
        "<body><h1>Settings saved</h1>"
        "<p>Restart device to apply changes.</p>"
        "<a class='btn' href='/'>Back</a>"
        "</body></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

esp_err_t web_server_start(bool is_ap_mode)
{
    server_ap_mode = is_ap_mode;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = is_ap_mode ? 80 : 8080;
    config.max_uri_handlers = 16;
    config.stack_size = is_ap_mode ? 4096 : 6144;

    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(err));
        return err;
    }

    if (is_ap_mode) {
        /* AP mode: minimal routes for WiFi config only. OTA is deliberately NOT
         * exposed here - the setup AP uses a well-known password, so an
         * unauthenticated flash endpoint on it would be trivially abusable. */
        const httpd_uri_t routes[] = {
            { .uri = "/",          .method = HTTP_GET,  .handler = handler_root_ap,        .user_ctx = NULL },
            { .uri = "/wifi",      .method = HTTP_GET,  .handler = handler_wifi,            .user_ctx = NULL },
            { .uri = "/wifi-save", .method = HTTP_POST, .handler = handler_wifi_save,       .user_ctx = NULL },
            { .uri = "/hotspot-detect.html",     .method = HTTP_GET, .handler = handler_hotspot_detect,   .user_ctx = NULL },
            { .uri = "/generate_204",            .method = HTTP_GET, .handler = handler_captive_redirect, .user_ctx = NULL },
            { .uri = "/connectivity-check.html", .method = HTTP_GET, .handler = handler_captive_redirect, .user_ctx = NULL },
        };
        for (int i = 0; i < (int)(sizeof(routes) / sizeof(routes[0])); i++) {
            httpd_register_uri_handler(server, &routes[i]);
        }
    } else {
        /* STA mode: full control panel */
        const httpd_uri_t routes[] = {
            { .uri = "/",           .method = HTTP_GET,  .handler = handler_root,      .user_ctx = NULL },
            { .uri = "/control",    .method = HTTP_POST, .handler = handler_control,   .user_ctx = NULL },
            { .uri = "/wifi",       .method = HTTP_GET,  .handler = handler_wifi,      .user_ctx = NULL },
            { .uri = "/wifi-save",  .method = HTTP_POST, .handler = handler_wifi_save, .user_ctx = NULL },
            { .uri = "/api/health", .method = HTTP_GET,  .handler = handler_api_health,.user_ctx = NULL },
            { .uri = "/api/metrics",.method = HTTP_GET,  .handler = handler_api_metrics,.user_ctx = NULL },
            { .uri = "/api/debug",  .method = HTTP_GET,  .handler = handler_api_debug,  .user_ctx = NULL },
            { .uri = "/settings",       .method = HTTP_GET,  .handler = handler_settings,      .user_ctx = NULL },
            { .uri = "/settings-save",  .method = HTTP_POST, .handler = handler_settings_save, .user_ctx = NULL },
            { .uri = "/ota",       .method = HTTP_GET,  .handler = handler_ota_page,   .user_ctx = NULL },
            { .uri = "/ota",       .method = HTTP_POST, .handler = handler_ota_upload, .user_ctx = NULL },
        };
        for (int i = 0; i < (int)(sizeof(routes) / sizeof(routes[0])); i++) {
            httpd_register_uri_handler(server, &routes[i]);
        }
    }

    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
    return ESP_OK;
}

void web_server_stop(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
    }
}
