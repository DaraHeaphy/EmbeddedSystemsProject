#pragma once

#include "esp_err.h"
#include <stdint.h>

// Configuration for MQTT telemetry sender
typedef struct {
    const char *broker_uri;    // e.g., "mqtt://alderaan.software-engineering.ie:1883"
    const char *client_id;     // e.g., "reactor_bridge_agent"
    const char *pub_topic;     // e.g., "reactor/sensors"
    uint32_t interval_ms;      // How often to send (milliseconds)
    uint32_t count;            // Number of telemetry packets to send (0 = infinite)
} telemetry_config_t;

// Start sending fake telemetry data via MQTT
// Returns ESP_OK on success
esp_err_t start_telemetry_sender(const telemetry_config_t *config);

// Stop the telemetry sender task
void stop_telemetry_sender(void);
