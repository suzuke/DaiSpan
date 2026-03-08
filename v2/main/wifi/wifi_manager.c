#include "wifi/wifi_manager.h"
#include "config/config_manager.h"
#include "common/debug.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
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
        if (len < 12) continue;

        /* Build DNS response: copy query, set response flags, add answer */
        uint8_t resp[512];
        memcpy(resp, buf, len);

        /* Set QR=1 (response), AA=1 (authoritative), RA=1 */
        resp[2] = 0x84;
        resp[3] = 0x00;
        /* Answer count = 1 */
        resp[6] = 0x00;
        resp[7] = 0x01;

        /* Append answer: pointer to name in query + A record -> 192.168.4.1 */
        int pos = len;
        resp[pos++] = 0xC0;  /* name pointer */
        resp[pos++] = 0x0C;  /* offset to name in query */
        resp[pos++] = 0x00; resp[pos++] = 0x01;  /* type A */
        resp[pos++] = 0x00; resp[pos++] = 0x01;  /* class IN */
        resp[pos++] = 0x00; resp[pos++] = 0x00;
        resp[pos++] = 0x00; resp[pos++] = 0x0A;  /* TTL 10s */
        resp[pos++] = 0x00; resp[pos++] = 0x04;  /* data length 4 */
        resp[pos++] = 192; resp[pos++] = 168;
        resp[pos++] = 4;   resp[pos++] = 1;      /* 192.168.4.1 */

        sendto(sock, resp, pos, 0,
               (struct sockaddr *)&client_addr, addr_len);
    }
}

static esp_err_t start_ap(void)
{
    ESP_LOGI(TAG, "Starting AP mode: DaiSpan-Config");

    wifi_config_t ap_config = {
        .ap = {
            .ssid = "DaiSpan-Config",
            .ssid_len = 14,
            .password = "12345678",
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

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    /* Force 802.11b/g/n and HT20 bandwidth for max compatibility */
    esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);

    ESP_ERROR_CHECK(esp_wifi_start());

    /* Conservative TX power - C3 SuperMini antenna is weak, too high causes issues */
    esp_wifi_set_max_tx_power(52);  /* 13dBm */
    ESP_LOGI(TAG, "AP started (APSTA mode, ch1, WPA/WPA2, 13dBm)");

    /* Start captive portal DNS server */
    xTaskCreate(dns_server_task, "dns_srv", 4096, NULL, 3, NULL);

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

    /* Delay before WiFi start to let S21 power supply stabilize */
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_ERROR_CHECK(esp_wifi_start());

    /* Use moderate TX power - not too high (brownout) nor too low (unreliable) */
    esp_wifi_set_max_tx_power(52);  /* 13dBm */

    ESP_LOGI(TAG, "Connecting to: %s", ssid);

    /* Wait for connection or failure (60s timeout) */
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(60000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected");
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
