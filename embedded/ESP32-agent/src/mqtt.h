#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef struct {
    uint32_t sample_id;
    float temp_c;
    float accel_mag;
    uint8_t state;
    uint8_t power;
} mqtt_telemetry_t;

typedef void (*mqtt_command_callback_t)(const char *data, int len);

typedef struct {
    const char *broker_uri;
    const char *client_id;
    const char *pub_topic;
    const char *cmd_topic;
    uint32_t interval_ms;
} mqtt_config_t;

void mqtt_set_command_callback(mqtt_command_callback_t cb);
esp_err_t mqtt_start(const mqtt_config_t *config);
esp_err_t mqtt_update_telemetry(const mqtt_telemetry_t *telemetry);
void mqtt_stop(void);
