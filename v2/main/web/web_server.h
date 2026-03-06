#pragma once
#include "esp_err.h"
#include <stdbool.h>

/* Start HTTP server. Uses port 80 in AP mode (captive portal), 8080 in STA mode */
esp_err_t web_server_start(bool ap_mode);

/* Stop HTTP server */
void web_server_stop(void);
