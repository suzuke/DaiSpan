#include "wifi/wifi_manager.h"
#include "config/config_manager.h"
#include "common/debug.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = LOG_TAG_WIFI;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAX_RETRY          15

static EventGroupHandle_t wifi_event_group;
static int retry_count = 0;
static bool connected = false;
static bool ap_mode = false;
static char ip_str[16] = "0.0.0.0";

static void event_handler(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        connected = false;
        if (ap_mode) {
            return;  /* Already switched to AP mode, ignore stale STA events */
        }
        if (retry_count < MAX_RETRY) {
            retry_count++;
            /* Progressive delay: 500ms -> 1s -> 2s */
            int delay_ms = (retry_count < 10) ? 500 : (retry_count < 20) ? 1000 : 2000;
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
            esp_wifi_connect();
            ESP_LOGI(TAG, "Retry %d/%d", retry_count, MAX_RETRY);
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Connection failed after %d retries", MAX_RETRY);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Connected: %s", ip_str);
        retry_count = 0;
        connected = true;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t start_ap(void)
{
    ESP_LOGI(TAG, "Starting AP mode: DaiSpan-Config");

    wifi_config_t ap_config = {
        .ap = {
            .ssid = "DaiSpan-Config",
            .ssid_len = 14,
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ap_mode = true;
    strcpy(ip_str, "192.168.4.1");
    return ESP_OK;
}

esp_err_t wifi_manager_init(void)
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Create both STA and AP netifs so either mode can be used */
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                &event_handler, NULL));

    /* Check if WiFi is configured */
    if (!config_is_wifi_configured()) {
        ESP_LOGI(TAG, "No WiFi config, starting AP");
        return start_ap();
    }

    /* Try STA mode */
    char ssid[33], pass[65];
    config_get_wifi_ssid(ssid, sizeof(ssid));
    config_get_wifi_password(pass, sizeof(pass));

    wifi_config_t sta_config = {0};
    strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
    strncpy((char *)sta_config.sta.password, pass, sizeof(sta_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Set TX power for C3 */
    esp_wifi_set_max_tx_power(44);  /* 11dBm */

    ESP_LOGI(TAG, "Connecting to: %s", ssid);

    /* Wait for connection or failure (60s timeout) */
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(60000));

    if (bits & WIFI_CONNECTED_BIT) {
        /* Lower TX power after connect to save power */
        esp_wifi_set_max_tx_power(34);  /* ~8.5dBm */
        ESP_LOGI(TAG, "WiFi connected, switched to low power TX");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "STA failed, falling back to AP mode");
    ap_mode = true;  /* Prevent stale disconnect events from retrying */
    esp_wifi_stop();
    return start_ap();
}

bool wifi_manager_is_connected(void)
{
    return connected;
}

const char *wifi_manager_get_ip(void)
{
    return ip_str;
}

int wifi_manager_get_rssi(void)
{
    wifi_ap_record_t info;
    if (esp_wifi_sta_get_ap_info(&info) == ESP_OK) return info.rssi;
    return 0;
}

bool wifi_manager_is_ap_mode(void)
{
    return ap_mode;
}
