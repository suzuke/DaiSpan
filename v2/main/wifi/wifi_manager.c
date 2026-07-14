#include "wifi/wifi_manager.h"
#include "wifi/dns_responder.h"
#include "config/config_manager.h"
#include "common/debug.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "mbedtls/platform_util.h"
#include <string.h>

static const char *TAG = LOG_TAG_WIFI;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAX_RETRY          15

static EventGroupHandle_t wifi_event_group;
static esp_timer_handle_t reconnect_timer;
static portMUX_TYPE wifi_state_lock = portMUX_INITIALIZER_UNLOCKED;
static int retry_count = 0;
static bool connected = false;
static bool ap_mode = false;
static char ip_str[16] = "0.0.0.0";

static void reconnect_timer_callback(void *arg)
{
    (void)arg;
    portENTER_CRITICAL(&wifi_state_lock);
    bool should_connect = !ap_mode;
    portEXIT_CRITICAL(&wifi_state_lock);
    if (should_connect) {
        esp_wifi_connect();
    }
}

static void schedule_reconnect(uint32_t delay_ms)
{
    if (esp_timer_is_active(reconnect_timer)) {
        esp_timer_stop(reconnect_timer);
    }
    esp_err_t err = esp_timer_start_once(reconnect_timer,
                                         (uint64_t)delay_ms * 1000U);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to schedule WiFi reconnect: %s",
                 esp_err_to_name(err));
    }
}

static void event_handler(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        portENTER_CRITICAL(&wifi_state_lock);
        bool should_connect = !ap_mode;
        portEXIT_CRITICAL(&wifi_state_lock);
        if (should_connect) {
            esp_wifi_connect();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        portENTER_CRITICAL(&wifi_state_lock);
        connected = false;
        bool already_ap = ap_mode;
        portEXIT_CRITICAL(&wifi_state_lock);
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        if (already_ap) {
            return;  /* Already switched to AP mode, ignore stale STA events */
        }
        if (retry_count < MAX_RETRY) {
            retry_count++;
            /* Progressive delay: 500ms -> 1s -> 2s */
            int delay_ms = (retry_count < 5) ? 500
                : (retry_count < 10) ? 1000 : 2000;
            schedule_reconnect((uint32_t)delay_ms);
            ESP_LOGI(TAG, "Retry %d/%d", retry_count, MAX_RETRY);
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Connection failed after %d retries; retrying in 30s",
                     MAX_RETRY);
            schedule_reconnect(30000);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        char new_ip[sizeof(ip_str)];
        snprintf(new_ip, sizeof(new_ip), IPSTR, IP2STR(&event->ip_info.ip));
        portENTER_CRITICAL(&wifi_state_lock);
        strlcpy(ip_str, new_ip, sizeof(ip_str));
        connected = true;
        portEXIT_CRITICAL(&wifi_state_lock);
        ESP_LOGI(TAG, "Connected: %s", new_ip);
        retry_count = 0;
        if (esp_timer_is_active(reconnect_timer)) {
            esp_timer_stop(reconnect_timer);
        }
        xEventGroupClearBits(wifi_event_group, WIFI_FAIL_BIT);
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/*
 * Minimal DNS server: responds to ALL queries with 192.168.4.1
 * This is required for captive portal detection on iOS/Android.
 */
static void dns_server_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS: socket failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "DNS: bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS server started");

    uint8_t buf[512];
    struct sockaddr_in client_addr;
    socklen_t addr_len;

    for (;;) {
        addr_len = sizeof(client_addr);
        int len = recvfrom(sock, buf, sizeof(buf), 0,
                           (struct sockaddr *)&client_addr, &addr_len);
        if (len <= 0) {
            continue;
        }
        uint8_t resp[512];
        size_t response_len = dns_build_captive_response(
            buf, (size_t)len, resp, sizeof(resp));
        if (response_len == 0) {
            continue;
        }

        sendto(sock, resp, response_len, 0,
               (struct sockaddr *)&client_addr, addr_len);
    }
}

static esp_err_t start_ap(void)
{
    ESP_LOGI(TAG, "Starting AP mode: DaiSpan-Config");
    portENTER_CRITICAL(&wifi_state_lock);
    ap_mode = true;
    connected = false;
    strlcpy(ip_str, "192.168.4.1", sizeof(ip_str));
    portEXIT_CRITICAL(&wifi_state_lock);
    if (reconnect_timer && esp_timer_is_active(reconnect_timer)) {
        esp_timer_stop(reconnect_timer);
    }

    char admin_token[CONFIG_ADMIN_TOKEN_LEN + 1] = {0};
    bool token_created = false;
    esp_err_t err = config_get_or_create_admin_token(
        admin_token, sizeof(admin_token), &token_created);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load AP credential: %s", esp_err_to_name(err));
        return err;
    }

    wifi_config_t ap_config = {
        .ap = {
            .ssid = "DaiSpan-Config",
            .ssid_len = 14,
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .ssid_hidden = 0,
            .beacon_interval = 100,
            .pmf_cfg = {
                .required = false,
            },
        },
    };
    strlcpy((char *)ap_config.ap.password, admin_token,
            sizeof(ap_config.ap.password));

    if (token_created) {
        /* One-time provisioning channel; capture this credential securely. */
        ESP_LOGW(TAG, "NEW provisioning credential: user=admin password=%s",
                 admin_token);
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    mbedtls_platform_zeroize(admin_token, sizeof(admin_token));
    mbedtls_platform_zeroize(ap_config.ap.password,
                             sizeof(ap_config.ap.password));

    /* Force 802.11b/g/n and HT20 bandwidth for max compatibility */
    esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);

    ESP_ERROR_CHECK(esp_wifi_start());

    /* Conservative TX power - C3 SuperMini antenna is weak, too high causes issues */
    esp_wifi_set_max_tx_power(52);  /* 13dBm */
    ESP_LOGI(TAG, "AP started (APSTA mode, ch1, WPA/WPA2, 13dBm)");

    /* Start captive portal DNS server */
    BaseType_t task_result = xTaskCreate(
        dns_server_task, "dns_srv", 4096, NULL, 3, NULL);
    if (task_result != pdPASS) {
        ESP_LOGE(TAG, "Failed to start captive DNS task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t wifi_manager_init(void)
{
    wifi_event_group = xEventGroupCreate();
    if (!wifi_event_group) {
        return ESP_ERR_NO_MEM;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = reconnect_timer_callback,
        .name = "wifi_reconnect",
    };
    esp_err_t timer_err = esp_timer_create(&timer_args, &reconnect_timer);
    if (timer_err != ESP_OK) {
        vEventGroupDelete(wifi_event_group);
        wifi_event_group = NULL;
        return timer_err;
    }

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
    char ssid[33] = {0};
    char pass[65] = {0};
    esp_err_t err = config_get_wifi_ssid(ssid, sizeof(ssid));
    if (err == ESP_OK) {
        err = config_get_wifi_password(pass, sizeof(pass));
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Stored WiFi credentials are incomplete: %s",
                 esp_err_to_name(err));
        mbedtls_platform_zeroize(pass, sizeof(pass));
        return start_ap();
    }

    wifi_config_t sta_config = {0};
    memcpy(sta_config.sta.ssid, ssid, strlen(ssid));
    memcpy(sta_config.sta.password, pass, strlen(pass));
    mbedtls_platform_zeroize(pass, sizeof(pass));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    mbedtls_platform_zeroize(sta_config.sta.password,
                             sizeof(sta_config.sta.password));
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
    portENTER_CRITICAL(&wifi_state_lock);
    ap_mode = true;  /* Prevent stale disconnect events from retrying */
    portEXIT_CRITICAL(&wifi_state_lock);
    esp_wifi_stop();
    return start_ap();
}

bool wifi_manager_is_connected(void)
{
    portENTER_CRITICAL(&wifi_state_lock);
    bool result = connected;
    portEXIT_CRITICAL(&wifi_state_lock);
    return result;
}

bool wifi_manager_get_ip(char *buf, size_t len)
{
    if (!buf || len == 0) {
        return false;
    }
    portENTER_CRITICAL(&wifi_state_lock);
    strlcpy(buf, ip_str, len);
    portEXIT_CRITICAL(&wifi_state_lock);
    return true;
}

int wifi_manager_get_rssi(void)
{
    wifi_ap_record_t info;
    if (esp_wifi_sta_get_ap_info(&info) == ESP_OK) return info.rssi;
    return 0;
}

bool wifi_manager_is_ap_mode(void)
{
    portENTER_CRITICAL(&wifi_state_lock);
    bool result = ap_mode;
    portEXIT_CRITICAL(&wifi_state_lock);
    return result;
}
