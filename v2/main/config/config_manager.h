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

/* Per-device administrator token (32 lowercase hexadecimal characters). */
#define CONFIG_ADMIN_TOKEN_LEN 32
esp_err_t config_get_or_create_admin_token(char *buf, size_t len, bool *created);

/* HomeKit config */
esp_err_t config_get_hk_setup_code(char *buf, size_t len);
esp_err_t config_get_or_create_hk_setup_code(char *buf, size_t len,
                                              bool *created);
esp_err_t config_set_hk_setup_code(const char *code);
esp_err_t config_get_hk_name(char *buf, size_t len);        /* default "DaiSpan" */
esp_err_t config_set_hk_name(const char *name);
