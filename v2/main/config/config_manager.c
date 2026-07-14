#include "config/config_manager.h"
#include "common/debug.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_random.h"
#include "mbedtls/platform_util.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = LOG_TAG_CFG;
#define NVS_NAMESPACE "daispan"

esp_err_t config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_LOGI(TAG, "NVS initialized");
    return err;
}

static esp_err_t nvs_get(const char *key, char *buf, size_t len)
{
    if (!key || !buf || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    err = nvs_get_str(handle, key, buf, &len);
    nvs_close(handle);
    return err;
}

static esp_err_t nvs_set(const char *key, const char *value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

bool config_is_wifi_configured(void)
{
    char ssid[33] = {0};
    return nvs_get("wifi_ssid", ssid, sizeof(ssid)) == ESP_OK && strlen(ssid) > 0;
}

esp_err_t config_get_wifi_ssid(char *buf, size_t len)
{
    return nvs_get("wifi_ssid", buf, len);
}

esp_err_t config_get_wifi_password(char *buf, size_t len)
{
    return nvs_get("wifi_pass", buf, len);
}

esp_err_t config_set_wifi_credentials(const char *ssid, const char *password)
{
    if (!ssid || !password) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t ssid_len = strlen(ssid);
    size_t password_len = strlen(password);
    if (ssid_len == 0 || ssid_len > 32 || password_len > 63) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(handle, "wifi_ssid", ssid);
    if (err == ESP_OK) err = nvs_set_str(handle, "wifi_pass", password);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi credentials saved: %s", ssid);
    }
    return err;
}

static bool valid_admin_token(const char *token)
{
    if (!token || strlen(token) != CONFIG_ADMIN_TOKEN_LEN) {
        return false;
    }
    for (size_t i = 0; i < CONFIG_ADMIN_TOKEN_LEN; i++) {
        if (!((token[i] >= '0' && token[i] <= '9') ||
              (token[i] >= 'a' && token[i] <= 'f'))) {
            return false;
        }
    }
    return true;
}

esp_err_t config_get_or_create_admin_token(char *buf, size_t len, bool *created)
{
    static const char hex[] = "0123456789abcdef";
    uint8_t random_bytes[CONFIG_ADMIN_TOKEN_LEN / 2];

    if (!buf || len < CONFIG_ADMIN_TOKEN_LEN + 1) {
        return ESP_ERR_INVALID_ARG;
    }
    if (created) {
        *created = false;
    }

    esp_err_t err = nvs_get("admin_token", buf, len);
    if (err == ESP_OK && valid_admin_token(buf)) {
        return ESP_OK;
    }

    esp_fill_random(random_bytes, sizeof(random_bytes));
    for (size_t i = 0; i < sizeof(random_bytes); i++) {
        buf[i * 2] = hex[random_bytes[i] >> 4];
        buf[i * 2 + 1] = hex[random_bytes[i] & 0x0f];
    }
    buf[CONFIG_ADMIN_TOKEN_LEN] = '\0';

    err = nvs_set("admin_token", buf);
    mbedtls_platform_zeroize(random_bytes, sizeof(random_bytes));
    if (err != ESP_OK) {
        mbedtls_platform_zeroize(buf, len);
        return err;
    }

    if (created) {
        *created = true;
    }
    return ESP_OK;
}

esp_err_t config_get_hk_setup_code(char *buf, size_t len)
{
    return config_get_or_create_hk_setup_code(buf, len, NULL);
}

static bool valid_hk_setup_code(const char *code)
{
    if (!code || strlen(code) != 10 || code[3] != '-' || code[6] != '-') {
        return false;
    }

    char digits[9];
    size_t di = 0;
    for (size_t i = 0; i < 10; i++) {
        if (i == 3 || i == 6) continue;
        if (code[i] < '0' || code[i] > '9') return false;
        digits[di++] = code[i];
    }
    digits[di] = '\0';

    bool all_same = true;
    for (size_t i = 1; i < 8; i++) {
        if (digits[i] != digits[0]) {
            all_same = false;
            break;
        }
    }
    return !all_same && strcmp(digits, "12345678") != 0 &&
           strcmp(digits, "87654321") != 0 &&
           strcmp(digits, "11122333") != 0;
}

esp_err_t config_get_or_create_hk_setup_code(char *buf, size_t len,
                                              bool *created)
{
    if (!buf || len < 11) {
        return ESP_ERR_INVALID_ARG;
    }
    if (created) {
        *created = false;
    }

    esp_err_t err = nvs_get("hk_code", buf, len);
    if (err == ESP_OK && valid_hk_setup_code(buf)) {
        return ESP_OK;
    }

    do {
        uint32_t value = esp_random() % 100000000U;
        snprintf(buf, len, "%03lu-%02lu-%03lu",
                 (unsigned long)(value / 100000U),
                 (unsigned long)((value / 1000U) % 100U),
                 (unsigned long)(value % 1000U));
    } while (!valid_hk_setup_code(buf));

    err = nvs_set("hk_code", buf);
    if (err == ESP_OK && created) {
        *created = true;
    }
    return err;
}

esp_err_t config_set_hk_setup_code(const char *code)
{
    if (!valid_hk_setup_code(code)) {
        return ESP_ERR_INVALID_ARG;
    }
    return nvs_set("hk_code", code);
}

esp_err_t config_get_hk_name(char *buf, size_t len)
{
    if (!buf || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = nvs_get("hk_name", buf, len);
    if (err != ESP_OK) {
        strncpy(buf, "DaiSpan", len);
        buf[len - 1] = '\0';
    }
    return ESP_OK;
}

esp_err_t config_set_hk_name(const char *name)
{
    if (!name || name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    return nvs_set("hk_name", name);
}
