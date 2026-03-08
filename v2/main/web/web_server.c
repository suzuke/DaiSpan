#include "web/web_server.h"
#include "config/config_manager.h"
#include "controller/thermostat_ctrl.h"
#include "s21/s21_protocol.h"
#include "wifi/wifi_manager.h"
#include "common/debug.h"
#include <esp_http_server.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = LOG_TAG_WEB;
static httpd_handle_t server = NULL;

static esp_err_t handler_root(httpd_req_t *req)
{
    char buf[1024];
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
        "<p>Controller: %s (errors: %lu)</p>"
        "</div>"
        "<div><a class='btn' href='/wifi'>WiFi</a>"
        "<a class='btn' href='/ota'>OTA</a>"
        "<a class='btn' href='/api/health'>Health</a>"
        "<a class='btn' href='/api/metrics'>Metrics</a></div>"
        "</body></html>",
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long)hr, (unsigned long)min, (unsigned long)sec,
        wifi_manager_get_ip(), wifi_manager_get_rssi(),
        thermostat_ctrl_get_power() ? "ON" : "OFF",
        thermostat_ctrl_get_current_temp(),
        thermostat_ctrl_get_target_temp(),
        thermostat_ctrl_is_healthy() ? "Healthy" : "Error",
        (unsigned long)thermostat_ctrl_get_error_count()
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
        "<form method='POST' action='/wifi-save'>"
        "<label>SSID</label><input name='ssid' required>"
        "<label>Password</label><input name='password' type='password'>"
        "<br><button type='submit'>Save &amp; Restart</button>"
        "</form></body></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

/* URL-decode in place: %XX -> char, '+' -> ' ' */
static void url_decode(char *dst, const char *src, size_t dst_size)
{
    size_t di = 0;
    while (*src && di < dst_size - 1) {
        if (*src == '%' && src[1] && src[2]) {
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
        (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000));
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
        (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000),
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
        "<br><button type='submit'>Upload</button>"
        "</form>"
        "<div id='progress'><p>Uploading... please wait</p></div>"
        "<script>document.getElementById('f').onsubmit=function(){"
        "document.getElementById('progress').style.display='block'}</script>"
        "</body></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handler_ota_upload(httpd_req_t *req)
{
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
    int total_written = 0;

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

        /* Skip multipart header on first chunk (find \r\n\r\n boundary) */
        if (!header_skipped) {
            char *body = NULL;
            for (int i = 0; i < recv_len - 3; i++) {
                if (buf[i] == '\r' && buf[i+1] == '\n' && buf[i+2] == '\r' && buf[i+3] == '\n') {
                    body = &buf[i + 4];
                    break;
                }
            }
            if (body) {
                data = body;
                data_len = recv_len - (int)(body - buf);
                header_skipped = true;
            } else {
                remaining -= recv_len;
                continue;
            }
        }

        /* Strip trailing multipart boundary if present at end */
        /* (simplified: we rely on esp_ota_end to validate the image) */

        err = esp_ota_write(ota_handle, data, data_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
        }

        total_written += data_len;
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
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = is_ap_mode ? 80 : 8080;
    config.max_uri_handlers = 16;
    config.stack_size = 4096;

    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t routes[] = {
        { .uri = "/",           .method = HTTP_GET,  .handler = handler_root,      .user_ctx = NULL },
        { .uri = "/wifi",       .method = HTTP_GET,  .handler = handler_wifi,      .user_ctx = NULL },
        { .uri = "/wifi-save",  .method = HTTP_POST, .handler = handler_wifi_save, .user_ctx = NULL },
        { .uri = "/api/health", .method = HTTP_GET,  .handler = handler_api_health,.user_ctx = NULL },
        { .uri = "/api/metrics",.method = HTTP_GET,  .handler = handler_api_metrics,.user_ctx = NULL },
        { .uri = "/api/debug", .method = HTTP_GET,  .handler = handler_api_debug,  .user_ctx = NULL },
        { .uri = "/ota",       .method = HTTP_GET,  .handler = handler_ota_page,   .user_ctx = NULL },
        { .uri = "/ota",       .method = HTTP_POST, .handler = handler_ota_upload, .user_ctx = NULL },
        /* Apple/Android captive portal detection endpoints */
        { .uri = "/hotspot-detect.html",   .method = HTTP_GET, .handler = handler_hotspot_detect,  .user_ctx = NULL },
        { .uri = "/generate_204",          .method = HTTP_GET, .handler = handler_captive_redirect, .user_ctx = NULL },
        { .uri = "/connectivity-check.html", .method = HTTP_GET, .handler = handler_captive_redirect, .user_ctx = NULL },
    };

    for (int i = 0; i < (int)(sizeof(routes) / sizeof(routes[0])); i++) {
        httpd_register_uri_handler(server, &routes[i]);
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
