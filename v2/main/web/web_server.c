#include "web/web_server.h"
#include "config/config_manager.h"
#include "controller/thermostat_ctrl.h"
#include "wifi/wifi_manager.h"
#include "common/debug.h"
#include <esp_http_server.h>
#include <esp_system.h>
#include <esp_random.h>
#include <esp_ota_ops.h>
#include "mbedtls/base64.h"
#include "mbedtls/constant_time.h"
#include "mbedtls/platform_util.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = LOG_TAG_WEB;
static httpd_handle_t server = NULL;
static bool server_ap_mode = false;
static char expected_authorization[80];
static char csrf_token[CONFIG_ADMIN_TOKEN_LEN + 1];

static esp_err_t send_sized(httpd_req_t *req, const char *buf,
                            size_t capacity, int len)
{
    if (len < 0 || (size_t)len >= capacity) {
        ESP_LOGE(TAG, "HTTP response buffer too small");
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Response too large");
    }
    return httpd_resp_send(req, buf, len);
}

static esp_err_t send_unauthorized(httpd_req_t *req)
{
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"DaiSpan\"");
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "Authentication required",
                           HTTPD_RESP_USE_STRLEN);
}

static bool request_is_admin(httpd_req_t *req)
{
    if (server_ap_mode) {
        return true;
    }

    char authorization[sizeof(expected_authorization)] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization", authorization,
                                    sizeof(authorization)) != ESP_OK) {
        return false;
    }

    size_t actual_len = strlen(authorization);
    size_t expected_len = strlen(expected_authorization);
    bool valid = actual_len == expected_len &&
                 mbedtls_ct_memcmp(authorization, expected_authorization,
                                   expected_len) == 0;
    mbedtls_platform_zeroize(authorization, sizeof(authorization));
    return valid;
}

static bool valid_csrf_token(const char *token)
{
    if (!token || strlen(token) != CONFIG_ADMIN_TOKEN_LEN) {
        return false;
    }
    return mbedtls_ct_memcmp(token, csrf_token, CONFIG_ADMIN_TOKEN_LEN) == 0;
}

static bool request_has_valid_csrf_header(httpd_req_t *req)
{
    char token[CONFIG_ADMIN_TOKEN_LEN + 1] = {0};
    bool valid = httpd_req_get_hdr_value_str(
        req, "X-CSRF-Token", token, sizeof(token)) == ESP_OK &&
        valid_csrf_token(token);
    mbedtls_platform_zeroize(token, sizeof(token));
    return valid;
}

static esp_err_t initialize_web_credentials(bool *created)
{
    char admin_token[CONFIG_ADMIN_TOKEN_LEN + 1] = {0};
    char credentials[sizeof("admin:") + CONFIG_ADMIN_TOKEN_LEN] = {0};
    unsigned char encoded[64] = {0};
    uint8_t csrf_random[CONFIG_ADMIN_TOKEN_LEN / 2];
    size_t encoded_len = 0;

    esp_err_t err = config_get_or_create_admin_token(
        admin_token, sizeof(admin_token), created);
    if (err != ESP_OK) {
        return err;
    }

    int credential_len = snprintf(credentials, sizeof(credentials),
                                  "admin:%s", admin_token);
    if (credential_len < 0 || (size_t)credential_len >= sizeof(credentials) ||
        mbedtls_base64_encode(encoded, sizeof(encoded), &encoded_len,
                              (const unsigned char *)credentials,
                              (size_t)credential_len) != 0) {
        err = ESP_FAIL;
        goto cleanup;
    }

    int auth_len = snprintf(expected_authorization,
                            sizeof(expected_authorization), "Basic %.*s",
                            (int)encoded_len, encoded);
    if (auth_len < 0 || (size_t)auth_len >= sizeof(expected_authorization)) {
        err = ESP_FAIL;
        goto cleanup;
    }

    static const char hex[] = "0123456789abcdef";
    esp_fill_random(csrf_random, sizeof(csrf_random));
    for (size_t i = 0; i < sizeof(csrf_random); i++) {
        csrf_token[i * 2] = hex[csrf_random[i] >> 4];
        csrf_token[i * 2 + 1] = hex[csrf_random[i] & 0x0f];
    }
    csrf_token[CONFIG_ADMIN_TOKEN_LEN] = '\0';

    if (created && *created) {
        /* One-time provisioning channel for devices upgraded in STA mode. */
        ESP_LOGW(TAG, "NEW administrator credential: user=admin password=%s",
                 admin_token);
    }

cleanup:
    mbedtls_platform_zeroize(admin_token, sizeof(admin_token));
    mbedtls_platform_zeroize(credentials, sizeof(credentials));
    mbedtls_platform_zeroize(encoded, sizeof(encoded));
    mbedtls_platform_zeroize(csrf_random, sizeof(csrf_random));
    return err;
}

static esp_err_t handler_root(httpd_req_t *req)
{
    if (!request_is_admin(req)) return send_unauthorized(req);

    char buf[1024];
    char ip[16];
    thermostat_snapshot_t snapshot;
    thermostat_ctrl_get_snapshot(&snapshot);
    wifi_manager_get_ip(ip, sizeof(ip));
    uint32_t uptime = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
    uint32_t hr = uptime / 3600;
    uint32_t min = (uptime % 3600) / 60;
    uint32_t sec = uptime % 60;

    int len = snprintf(buf, sizeof(buf),
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta http-equiv='refresh' content='30'>"
        "<title>DaiSpan</title>"
        "<style>body{font-family:sans-serif;max-width:600px;margin:0 auto;padding:20px}"
        ".card{background:#f5f5f5;border-radius:8px;padding:15px;margin:10px 0}"
        ".btn{display:inline-block;padding:10px 20px;background:#007aff;color:#fff;"
        "text-decoration:none;border-radius:6px;margin:5px}</style></head><body>"
        "<h1>DaiSpan v2</h1>"
        "<div class='card'>"
        "<p>Memory: %lu bytes free</p>"
        "<p>Uptime: %lu:%02lu:%02lu</p>"
        "<p>WiFi: %s (%d dBm)</p>"
        "<p>Power: %s | Temp: %.1f / %.1f C</p>"
        "<p>Controller: %s (errors: %lu, pending: %s, rejected: %lu)</p>"
        "</div>"
        "<div><a class='btn' href='/wifi'>WiFi</a>"
        "<a class='btn' href='/ota'>OTA</a>"
        "<a class='btn' href='/api/health'>Health</a>"
        "<a class='btn' href='/api/metrics'>Metrics</a></div>"
        "</body></html>",
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long)hr, (unsigned long)min, (unsigned long)sec,
        ip, wifi_manager_get_rssi(),
        snapshot.desired.power ? "ON" : "OFF",
        snapshot.observed.valid ? snapshot.observed.current_temp
                                : snapshot.desired.current_temp,
        snapshot.desired.target_temp,
        snapshot.consecutive_errors < THERMOSTAT_CTRL_MAX_CONSECUTIVE_ERRORS
            ? "Healthy" : "Error",
        (unsigned long)snapshot.consecutive_errors,
        snapshot.pending ? "yes" : "no",
        (unsigned long)snapshot.dropped_commands
    );

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return send_sized(req, buf, sizeof(buf), len);
}

static esp_err_t handler_wifi(httpd_req_t *req)
{
    if (!request_is_admin(req)) return send_unauthorized(req);

    static const char *prefix =
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<title>WiFi Config</title>"
        "<style>body{font-family:sans-serif;max-width:400px;margin:0 auto;padding:20px}"
        "input{width:100%;padding:10px;margin:5px 0;box-sizing:border-box;"
        "border:1px solid #ccc;border-radius:4px}"
        "button{width:100%;padding:12px;background:#007aff;color:#fff;"
        "border:none;border-radius:6px;font-size:16px}</style></head>"
        "<body><h1>WiFi Config</h1>"
        "<form method='POST' action='/wifi-save'>"
        "<input type='hidden' name='csrf' value='";
    static const char *suffix =
        "'>"
        "<label>SSID</label><input name='ssid' required>"
        "<label>Password</label><input name='password' type='password'>"
        "<br><button type='submit'>Save &amp; Restart</button>"
        "</form></body></html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (httpd_resp_send_chunk(req, prefix, HTTPD_RESP_USE_STRLEN) != ESP_OK ||
        httpd_resp_send_chunk(req, csrf_token, HTTPD_RESP_USE_STRLEN) != ESP_OK ||
        httpd_resp_send_chunk(req, suffix, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
        return ESP_FAIL;
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* URL-decode in place: %XX -> char, '+' -> ' '. */
static bool url_decode(char *dst, const char *src, size_t src_len,
                       size_t dst_size)
{
    size_t di = 0;
    size_t si = 0;
    if (!dst || !src || dst_size == 0) return false;

    while (si < src_len && di < dst_size - 1) {
        if (src[si] == '%') {
            if (si + 2 >= src_len) return false;
            int high = hex_value(src[si + 1]);
            int low = hex_value(src[si + 2]);
            if (high < 0 || low < 0) return false;
            dst[di++] = (char)((high << 4) | low);
            si += 3;
        } else if (src[si] == '+') {
            dst[di++] = ' ';
            si++;
        } else {
            dst[di++] = src[si++];
        }
    }
    dst[di] = '\0';
    return si == src_len;
}

static bool form_get_value(const char *content, const char *key,
                           char *value, size_t value_size)
{
    size_t key_len = strlen(key);
    const char *field = content;

    while (field && *field) {
        const char *end = strchr(field, '&');
        size_t field_len = end ? (size_t)(end - field) : strlen(field);
        if (field_len > key_len && field[key_len] == '=' &&
            memcmp(field, key, key_len) == 0) {
            return url_decode(value, field + key_len + 1,
                              field_len - key_len - 1, value_size);
        }
        field = end ? end + 1 : NULL;
    }
    return false;
}

static esp_err_t read_request_body(httpd_req_t *req, char *buf,
                                   size_t capacity)
{
    if (req->content_len <= 0 || (size_t)req->content_len >= capacity) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_send(req, "Request body too large", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    size_t received = 0;
    while (received < (size_t)req->content_len) {
        int len = httpd_req_recv(req, buf + received,
                                 (size_t)req->content_len - received);
        if (len <= 0) {
            httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT,
                                "Request body timeout");
            return ESP_FAIL;
        }
        received += (size_t)len;
    }
    buf[received] = '\0';
    return ESP_OK;
}

static esp_err_t handler_wifi_save(httpd_req_t *req)
{
    if (!request_is_admin(req)) return send_unauthorized(req);

    char content[512];
    char ssid[33] = {0};
    char pass[65] = {0};
    char csrf[CONFIG_ADMIN_TOKEN_LEN + 1] = {0};

    esp_err_t read_result = read_request_body(req, content, sizeof(content));
    if (read_result != ESP_OK) {
        mbedtls_platform_zeroize(content, sizeof(content));
        return read_result;
    }

    if (!form_get_value(content, "csrf", csrf, sizeof(csrf)) ||
        !valid_csrf_token(csrf)) {
        mbedtls_platform_zeroize(content, sizeof(content));
        mbedtls_platform_zeroize(csrf, sizeof(csrf));
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN,
                                   "Invalid CSRF token");
    }
    if (!form_get_value(content, "ssid", ssid, sizeof(ssid)) ||
        strlen(ssid) == 0) {
        mbedtls_platform_zeroize(content, sizeof(content));
        mbedtls_platform_zeroize(csrf, sizeof(csrf));
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
    }
    if (!form_get_value(content, "password", pass, sizeof(pass))) {
        mbedtls_platform_zeroize(content, sizeof(content));
        mbedtls_platform_zeroize(pass, sizeof(pass));
        mbedtls_platform_zeroize(csrf, sizeof(csrf));
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Invalid WiFi password");
    }

    ESP_LOGI(TAG, "WiFi config: SSID='%s' pass_len=%d", ssid, (int)strlen(pass));
    esp_err_t err = config_set_wifi_credentials(ssid, pass);
    mbedtls_platform_zeroize(content, sizeof(content));
    mbedtls_platform_zeroize(pass, sizeof(pass));
    mbedtls_platform_zeroize(csrf, sizeof(csrf));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save WiFi config: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Failed to save WiFi config");
    }

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
        (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000));
    httpd_resp_set_type(req, "application/json");
    return send_sized(req, buf, sizeof(buf), len);
}

static esp_err_t handler_api_metrics(httpd_req_t *req)
{
    if (!request_is_admin(req)) return send_unauthorized(req);

    char buf[512];
    char ip[16];
    thermostat_snapshot_t snapshot;
    thermostat_ctrl_get_snapshot(&snapshot);
    wifi_manager_get_ip(ip, sizeof(ip));
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
        "\"observedPower\":%s,"
        "\"observedMode\":%d,"
        "\"stateValid\":%s,"
        "\"pending\":%s,"
        "\"droppedCommands\":%lu,"
        "\"healthy\":%s,"
        "\"errors\":%lu"
        "}",
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long)esp_get_minimum_free_heap_size(),
        (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000),
        wifi_manager_get_rssi(),
        ip,
        snapshot.desired.power ? "true" : "false",
        snapshot.desired.power ? snapshot.target_homekit_mode : HAP_MODE_OFF,
        snapshot.desired.target_temp,
        snapshot.observed.valid ? snapshot.observed.current_temp
                                : snapshot.desired.current_temp,
        snapshot.desired.fan_speed,
        snapshot.desired.swing_vertical ? "true" : "false",
        snapshot.observed.power ? "true" : "false",
        snapshot.observed.mode,
        snapshot.observed.valid ? "true" : "false",
        snapshot.pending ? "true" : "false",
        (unsigned long)snapshot.dropped_commands,
        snapshot.consecutive_errors < THERMOSTAT_CTRL_MAX_CONSECUTIVE_ERRORS
            ? "true" : "false",
        (unsigned long)snapshot.consecutive_errors
    );
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return send_sized(req, buf, sizeof(buf), len);
}

static esp_err_t handler_ota_page(httpd_req_t *req)
{
    if (!request_is_admin(req)) return send_unauthorized(req);

    static const char *prefix =
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<title>OTA Update</title>"
        "<style>body{font-family:sans-serif;max-width:400px;margin:0 auto;padding:20px}"
        "input[type=file]{margin:10px 0}"
        "button{width:100%;padding:12px;background:#007aff;color:#fff;"
        "border:none;border-radius:6px;font-size:16px}"
        "#progress{display:none;margin:10px 0}"
        "</style></head><body><h1>OTA Update</h1>"
        "<form id='f'>"
        "<input type='file' name='firmware' accept='.bin' required>"
        "<br><button type='submit'>Upload</button>"
        "</form>"
        "<div id='progress'><p id='message'>Uploading... please wait</p></div>"
        "<script>document.getElementById('f').onsubmit=async function(e){"
        "e.preventDefault();const p=document.getElementById('progress');"
        "const m=document.getElementById('message');p.style.display='block';"
        "const file=this.elements.firmware.files[0];try{const r=await fetch('/ota',{"
        "method:'POST',credentials:'same-origin',headers:{"
        "'Content-Type':'application/octet-stream','X-CSRF-Token':'";
    static const char *suffix =
        "'},body:file});m.textContent=await r.text();if(!r.ok)throw new Error(m.textContent);"
        "}catch(err){m.textContent='Update failed: '+err.message;}}</script>"
        "</body></html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (httpd_resp_send_chunk(req, prefix, HTTPD_RESP_USE_STRLEN) != ESP_OK ||
        httpd_resp_send_chunk(req, csrf_token, HTTPD_RESP_USE_STRLEN) != ESP_OK ||
        httpd_resp_send_chunk(req, suffix, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
        return ESP_FAIL;
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t handler_ota_upload(httpd_req_t *req)
{
    if (!request_is_admin(req)) return send_unauthorized(req);
    if (!request_has_valid_csrf_header(req)) {
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN,
                                   "Invalid CSRF token");
    }

    char content_type[48] = {0};
    if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type,
                                    sizeof(content_type)) != ESP_OK ||
        strcmp(content_type, "application/octet-stream") != 0) {
        httpd_resp_set_status(req, "415 Unsupported Media Type");
        return httpd_resp_send(req, "Expected application/octet-stream",
                               HTTPD_RESP_USE_STRLEN);
    }

    esp_ota_handle_t ota_handle = 0;
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);

    if (!update_partition) {
        ESP_LOGE(TAG, "OTA: no update partition found");
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
    }

    if (req->content_len <= 0 ||
        (size_t)req->content_len > update_partition->size) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        return httpd_resp_send(req, "Firmware size is invalid",
                               HTTPD_RESP_USE_STRLEN);
    }

    ESP_LOGI(TAG, "OTA: writing to partition '%s' at 0x%lx, size %lu",
             update_partition->label,
             (unsigned long)update_partition->address,
             (unsigned long)update_partition->size);

    esp_err_t err = esp_ota_begin(update_partition, (size_t)req->content_len,
                                  &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
    }

    uint8_t buf[1024];
    int remaining = req->content_len;
    size_t total_written = 0;
    unsigned int consecutive_timeouts = 0;

    while (remaining > 0) {
        size_t read_size = sizeof(buf) < (size_t)remaining
            ? sizeof(buf) : (size_t)remaining;
        int recv_len = httpd_req_recv(req, (char *)buf, read_size);
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT &&
                ++consecutive_timeouts < 5) {
                continue;
            }
            ESP_LOGE(TAG, "OTA: recv error");
            esp_ota_abort(ota_handle);
            return httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT,
                                       "Firmware upload timeout");
        }
        consecutive_timeouts = 0;

        err = esp_ota_write(ota_handle, buf, (size_t)recv_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
        }

        total_written += (size_t)recv_len;
        remaining -= recv_len;
    }

    ESP_LOGI(TAG, "OTA: %zu bytes written, finalizing...", total_written);

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

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "Update successful; restarting",
                    HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "OTA complete, restarting...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
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

esp_err_t web_server_start(bool is_ap_mode)
{
    server_ap_mode = is_ap_mode;
    bool credential_created = false;
    esp_err_t err = initialize_web_credentials(&credential_created);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize web credentials: %s",
                 esp_err_to_name(err));
        return err;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = is_ap_mode ? 80 : 8080;
    config.max_uri_handlers = 16;
    config.stack_size = is_ap_mode ? 4096 : 6144;

    err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(err));
        web_server_stop();
        return err;
    }

    if (is_ap_mode) {
        const httpd_uri_t routes[] = {
            { .uri = "/", .method = HTTP_GET, .handler = handler_captive_redirect, .user_ctx = NULL },
            { .uri = "/wifi", .method = HTTP_GET, .handler = handler_wifi, .user_ctx = NULL },
            { .uri = "/wifi-save", .method = HTTP_POST, .handler = handler_wifi_save, .user_ctx = NULL },
            { .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = handler_hotspot_detect, .user_ctx = NULL },
            { .uri = "/generate_204", .method = HTTP_GET, .handler = handler_captive_redirect, .user_ctx = NULL },
            { .uri = "/connectivity-check.html", .method = HTTP_GET, .handler = handler_captive_redirect, .user_ctx = NULL },
        };
        for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
            err = httpd_register_uri_handler(server, &routes[i]);
            if (err != ESP_OK) break;
        }
    } else {
        const httpd_uri_t routes[] = {
            { .uri = "/", .method = HTTP_GET, .handler = handler_root, .user_ctx = NULL },
            { .uri = "/wifi", .method = HTTP_GET, .handler = handler_wifi, .user_ctx = NULL },
            { .uri = "/wifi-save", .method = HTTP_POST, .handler = handler_wifi_save, .user_ctx = NULL },
            { .uri = "/api/health", .method = HTTP_GET, .handler = handler_api_health, .user_ctx = NULL },
            { .uri = "/api/metrics", .method = HTTP_GET, .handler = handler_api_metrics, .user_ctx = NULL },
            { .uri = "/ota", .method = HTTP_GET, .handler = handler_ota_page, .user_ctx = NULL },
            { .uri = "/ota", .method = HTTP_POST, .handler = handler_ota_upload, .user_ctx = NULL },
        };
        for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
            err = httpd_register_uri_handler(server, &routes[i]);
            if (err != ESP_OK) break;
        }
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register HTTP routes: %s",
                 esp_err_to_name(err));
        web_server_stop();
        return err;
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
    mbedtls_platform_zeroize(expected_authorization,
                             sizeof(expected_authorization));
    mbedtls_platform_zeroize(csrf_token, sizeof(csrf_token));
}
