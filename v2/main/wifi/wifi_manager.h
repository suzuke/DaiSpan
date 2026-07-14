#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

/**
 * Initialize WiFi in STA mode and connect using stored credentials.
 * If no credentials or connection fails, starts SoftAP for config.
 */
esp_err_t wifi_manager_init(void);

/* Returns true if connected to STA */
bool wifi_manager_is_connected(void);

/* Copy the current IP address (e.g. "192.168.50.23") into the caller buffer. */
bool wifi_manager_get_ip(char *buf, size_t len);

/* Get RSSI of current AP connection */
int wifi_manager_get_rssi(void);

/* Returns true if in AP config mode */
bool wifi_manager_is_ap_mode(void);
