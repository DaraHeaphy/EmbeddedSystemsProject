#pragma once

#include "esp_err.h"
#include <stdint.h>

// Latest telemetry sample received from the reactor (UART link).
// This is what gets published upstream via MQTT as JSON.
typedef struct {
    uint32_t sample_id;
    float    temp_c;
    float    accel_mag;
    uint8_t  state;     // 0=NORMAL, 1=WARNING, 2=SCRAM
    uint8_t  power;     // 0..100 (%)
} reactor_telemetry_t;

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

// Start the MQTT telemetry bridge.
// Telemetry to publish is provided via telemetry_sender_update().
// Returns ESP_OK on success
esp_err_t start_telemetry_sender(const telemetry_config_t *config);

// Update the latest reactor telemetry sample to be published.
// Non-blocking; overwrites any previous pending sample.
esp_err_t telemetry_sender_update(const reactor_telemetry_t *telemetry);

// Stop the telemetry sender task
void stop_telemetry_sender(void);
