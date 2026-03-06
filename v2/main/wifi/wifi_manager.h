#pragma once
#include "esp_err.h"
#include <stdbool.h>

/**
 * Initialize WiFi in STA mode and connect using stored credentials.
 * If no credentials or connection fails, starts SoftAP for config.
 */
esp_err_t wifi_manager_init(void);

/* Returns true if connected to STA */
bool wifi_manager_is_connected(void);

/* Get IP address as string (e.g. "192.168.50.23") */
const char *wifi_manager_get_ip(void);

/* Get RSSI of current AP connection */
int wifi_manager_get_rssi(void);

/* Returns true if in AP config mode */
bool wifi_manager_is_ap_mode(void);
