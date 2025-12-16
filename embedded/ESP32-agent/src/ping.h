#pragma once

#include "esp_err.h"

// Ping a hostname (e.g., "google.com") and print results
// count: number of ping requests to send (0 = infinite)
esp_err_t ping_host(const char *hostname, uint32_t count);
