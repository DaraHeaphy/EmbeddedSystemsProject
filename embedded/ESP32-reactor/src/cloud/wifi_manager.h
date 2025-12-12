// wifi_manager.h
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

// Simple WiFi configuration
typedef struct {
    const char* ssid;
    const char* password;  // NULL for open networks
} wifi_config_simple_t;

// Initialize WiFi and connect
esp_err_t wifi_manager_init(wifi_config_simple_t* config);

// Check if connected
bool wifi_manager_is_connected(void);

// Disconnect and cleanup
void wifi_manager_cleanup(void);

#endif // WIFI_MANAGER_H

