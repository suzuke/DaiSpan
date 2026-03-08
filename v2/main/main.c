#include "common/debug.h"
#include "config/config_manager.h"
#include "wifi/wifi_manager.h"
#include "s21/s21_protocol.h"
#include "controller/thermostat_ctrl.h"
#include "homekit/hk_thermostat.h"
#include "web/web_server.h"

#include <hap.h>
#include <esp_system.h>
#include <esp_mac.h>
#include <mdns.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

/* ESP32-C3 SuperMini onboard LED */
#define STATUS_LED_PIN GPIO_NUM_8

static const char *TAG = LOG_TAG_MAIN;

static void led_init(void)
{
    gpio_reset_pin(STATUS_LED_PIN);
    gpio_set_direction(STATUS_LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(STATUS_LED_PIN, 1); /* LED off (active low) */
}

/* Blink patterns: fast=connecting, slow=AP mode, solid=connected */
static void led_blink(int on_ms, int off_ms, int count)
{
    for (int i = 0; i < count; i++) {
        gpio_set_level(STATUS_LED_PIN, 0); /* on */
        vTaskDelay(pdMS_TO_TICKS(on_ms));
        gpio_set_level(STATUS_LED_PIN, 1); /* off */
        vTaskDelay(pdMS_TO_TICKS(off_ms));
    }
}

static void hap_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event, void *data)
{
    switch (event) {
        case HAP_EVENT_PAIRING_STARTED:
            ESP_LOGI(TAG, "HomeKit pairing started");
            break;
        case HAP_EVENT_PAIRING_ABORTED:
            ESP_LOGW(TAG, "HomeKit pairing aborted");
            break;
        case HAP_EVENT_CTRL_PAIRED:
            ESP_LOGI(TAG, "HomeKit controller paired");
            break;
        case HAP_EVENT_CTRL_UNPAIRED:
            ESP_LOGI(TAG, "HomeKit controller unpaired");
            break;
        default:
            break;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "DaiSpan v2 starting...");
    ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());

    /* 0. LED init - blink once to confirm boot */
    led_init();
    led_blink(200, 200, 2);

    /* 1. Config (NVS) */
    ESP_ERROR_CHECK(config_init());

    /* 2. WiFi */
    led_blink(100, 100, 3); /* fast blink = WiFi connecting */
    ESP_ERROR_CHECK(wifi_manager_init());

    if (!wifi_manager_is_connected()) {
        ESP_LOGW(TAG, "No WiFi connection, running in AP config mode");
        web_server_start(true);  /* port 80, captive portal */
        while (1) {
            led_blink(500, 1500, 1); /* slow blink = AP mode */
        }
    }
    /* WiFi connected: solid LED on */
    gpio_set_level(STATUS_LED_PIN, 0);

    /* 3. S21 Protocol (UART init) */
    ESP_ERROR_CHECK(s21_init());

    /* 4. Controller (creates its own FreeRTOS task) */
    ESP_ERROR_CHECK(thermostat_ctrl_init());

    /* 5. HomeKit (HAP framework) */
    hap_init(HAP_TRANSPORT_WIFI);

    char setup_code[16];
    char device_name[32];
    config_get_hk_setup_code(setup_code, sizeof(setup_code));
    config_get_hk_name(device_name, sizeof(device_name));

    hap_set_setup_code(setup_code);
    hap_set_setup_id("DS01");

    hap_acc_t *accessory = hk_thermostat_create();
    if (!accessory) {
        ESP_LOGE(TAG, "Failed to create HomeKit accessory");
        esp_restart();
    }
    hap_add_accessory(accessory);

    /* Add Wi-Fi Transport service (HAP Spec R16) */
    hap_acc_add_wifi_transport_service(accessory, 0);

    esp_event_handler_register(HAP_EVENT, ESP_EVENT_ANY_ID,
                                &hap_event_handler, NULL);

    hap_start();
    ESP_LOGI(TAG, "HomeKit started: %s (code: %s)", device_name, setup_code);

    /* Give HAP framework time to register mDNS, then force-register as fallback */
    vTaskDelay(pdMS_TO_TICKS(3000));
    {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        mdns_txt_item_t txt[] = {
            {"id", mac_str},
            {"md", device_name},
            {"pv", "1.1"},
            {"s#", "1"},
            {"c#", "1"},
            {"ci", "9"},       /* category: thermostat */
            {"ff", "0"},
            {"sf", "1"},
        };
        esp_err_t add_err = mdns_service_add(device_name, "_hap", "_tcp", 80, txt, 8);
        ESP_LOGI(TAG, "mDNS _hap._tcp register: %s (may be ESP_ERR_INVALID_ARG if already exists)",
                 esp_err_to_name(add_err));
    }

    /* 6. Web server */
    web_server_start(false);  /* port 8080, normal mode */

    ESP_LOGI(TAG, "System ready. Free heap: %lu bytes",
             (unsigned long)esp_get_free_heap_size());

    /* Main loop: sync HomeKit state periodically */
    while (1) {
        hk_thermostat_sync();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
