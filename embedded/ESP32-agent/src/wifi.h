#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Initialize WiFi in station mode and connect to the configured AP
// Blocks until connected or times out
esp_err_t wifi_init_sta(void);

// Check if WiFi is connected
bool wifi_is_connected(void);
