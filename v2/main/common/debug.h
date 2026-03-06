#pragma once
#include "esp_log.h"

#define LOG_TAG_S21     "S21"
#define LOG_TAG_CTRL    "CTRL"
#define LOG_TAG_HK      "HK"
#define LOG_TAG_WIFI    "WIFI"
#define LOG_TAG_WEB     "WEB"
#define LOG_TAG_CFG     "CFG"
#define LOG_TAG_MAIN    "MAIN"

/*
 * Use ESP-IDF log macros directly with these tags:
 *   ESP_LOGE(LOG_TAG_S21, "error: %s", msg);
 *   ESP_LOGW(LOG_TAG_CTRL, "warning: %d", val);
 *   ESP_LOGI(LOG_TAG_HK, "info message");
 *   ESP_LOGD(LOG_TAG_WIFI, "debug: %s", detail);
 *   ESP_LOGV(LOG_TAG_WEB, "verbose trace");
 */
