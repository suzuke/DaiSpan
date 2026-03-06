#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

/* Initialize NVS-based config (namespace "daispan") */
esp_err_t config_init(void);

/* WiFi credentials */
bool config_is_wifi_configured(void);
esp_err_t config_get_wifi_ssid(char *buf, size_t len);
esp_err_t config_get_wifi_password(char *buf, size_t len);
esp_err_t config_set_wifi_credentials(const char *ssid, const char *password);

/* HomeKit config */
esp_err_t config_get_hk_setup_code(char *buf, size_t len);  /* default "111-22-333" */
esp_err_t config_set_hk_setup_code(const char *code);
esp_err_t config_get_hk_name(char *buf, size_t len);        /* default "DaiSpan" */
esp_err_t config_set_hk_name(const char *name);
