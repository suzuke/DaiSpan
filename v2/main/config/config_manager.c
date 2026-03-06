#include "config/config_manager.h"
#include "common/debug.h"
#include "nvs_flash.h"
#include "nvs.h"
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
    char ssid[33];
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
    esp_err_t err = nvs_set("wifi_ssid", ssid);
    if (err == ESP_OK) err = nvs_set("wifi_pass", password);
    ESP_LOGI(TAG, "WiFi credentials saved: %s", ssid);
    return err;
}

esp_err_t config_get_hk_setup_code(char *buf, size_t len)
{
    esp_err_t err = nvs_get("hk_code", buf, len);
    if (err != ESP_OK) {
        strncpy(buf, "111-22-333", len);
        buf[len - 1] = '\0';
    }
    return ESP_OK;
}

esp_err_t config_set_hk_setup_code(const char *code)
{
    return nvs_set("hk_code", code);
}

esp_err_t config_get_hk_name(char *buf, size_t len)
{
    esp_err_t err = nvs_get("hk_name", buf, len);
    if (err != ESP_OK) {
        strncpy(buf, "DaiSpan", len);
        buf[len - 1] = '\0';
    }
    return ESP_OK;
}

esp_err_t config_set_hk_name(const char *name)
{
    return nvs_set("hk_name", name);
}
