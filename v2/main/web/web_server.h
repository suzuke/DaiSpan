#pragma once
#include "esp_err.h"

/* Start HTTP server on port 8080 */
esp_err_t web_server_start(void);

/* Stop HTTP server */
void web_server_stop(void);
