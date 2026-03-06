#include "common/debug.h"
#include "config/config_manager.h"
#include "wifi/wifi_manager.h"
#include "s21/s21_protocol.h"
#include "controller/thermostat_ctrl.h"
#include "homekit/hk_thermostat.h"
#include "web/web_server.h"

#include <hap.h>
#include <esp_system.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = LOG_TAG_MAIN;

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

    /* 1. Config (NVS) */
    ESP_ERROR_CHECK(config_init());

    /* 2. WiFi */
    ESP_ERROR_CHECK(wifi_manager_init());

    if (!wifi_manager_is_connected()) {
        ESP_LOGW(TAG, "No WiFi connection, running in AP config mode");
        web_server_start();
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

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

    /* 6. Web server */
    web_server_start();

    ESP_LOGI(TAG, "System ready. Free heap: %lu bytes",
             (unsigned long)esp_get_free_heap_size());

    /* Main loop: sync HomeKit state periodically */
    while (1) {
        hk_thermostat_sync();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
