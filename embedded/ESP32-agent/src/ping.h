#pragma once

#include "esp_err.h"
#include <stdint.h>

// Callback invoked when an MQTT command is received
typedef void (*mqtt_cmd_callback_t)(const char *data, int data_len);

// Configuration for MQTT telemetry sender
typedef struct {
    const char *broker_uri;    // e.g., "mqtt://alderaan.software-engineering.ie:1883"
    const char *client_id;     // e.g., "reactor_bridge_agent"
    const char *pub_topic;     // e.g., "reactor/sensors"
    const char *cmd_topic;     // e.g., "reactor/commands" (subscribe for commands, NULL to disable)
    uint32_t interval_ms;      // How often to send (milliseconds)
    uint32_t count;            // Number of telemetry packets to send (0 = infinite)
} telemetry_config_t;

// Set callback for incoming MQTT commands (call before start_telemetry_sender)
void set_mqtt_cmd_callback(mqtt_cmd_callback_t cb);

// Start sending fake telemetry data via MQTT
// Returns ESP_OK on success
esp_err_t start_telemetry_sender(const telemetry_config_t *config);

// Stop the telemetry sender task
void stop_telemetry_sender(void);
