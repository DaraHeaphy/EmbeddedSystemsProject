// cloud_publisher.c
#include "cloud_publisher.h"
#include "mqtt_handler.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char* TAG = "CLOUD_PUB";

// Convert reactor_telemetry_t to JSON string
static void telemetry_to_json(const reactor_telemetry_t* t, char* buffer, size_t size)
{
    const char* state_str;
    switch (t->state) {
        case REACTOR_STATE_NORMAL:  state_str = "NORMAL"; break;
        case REACTOR_STATE_WARNING: state_str = "WARNING"; break;
        case REACTOR_STATE_SCRAM:   state_str = "SCRAM"; break;
        default:                    state_str = "UNKNOWN"; break;
    }

    snprintf(buffer, size,
             "{\"sample_id\":%lu,\"temp\":%.2f,\"accel_mag\":%.3f,\"state\":\"%s\",\"power\":%u}",
             (unsigned long)t->sample_id,
             t->temperature_c,
             t->accel_mag,
             state_str,
             t->power_percent);
}

esp_err_t cloud_publisher_publish_telemetry(const reactor_telemetry_t* telemetry)
{
    if (!telemetry) {
        ESP_LOGE(TAG, "NULL telemetry pointer");
        return ESP_ERR_INVALID_ARG;
    }

    // Check if MQTT is connected
    if (!mqtt_handler_is_connected()) {
        ESP_LOGW(TAG, "MQTT not connected, skipping publish");
        return ESP_ERR_INVALID_STATE;
    }

    // Convert to JSON
    char json_buffer[256];
    telemetry_to_json(telemetry, json_buffer, sizeof(json_buffer));

    // Publish via MQTT
    return mqtt_handler_publish_json(json_buffer);
}

esp_err_t cloud_publisher_publish_alert(const char* level, const char* message)
{
    if (!level || !message) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!mqtt_handler_is_connected()) {
        ESP_LOGW(TAG, "MQTT not connected, skipping alert");
        return ESP_ERR_INVALID_STATE;
    }

    // Build alert JSON
    char json_buffer[256];
    snprintf(json_buffer, sizeof(json_buffer),
             "{\"level\":\"%s\",\"message\":\"%s\"}",
             level, message);

    // Publish to alerts topic
    return mqtt_handler_publish_json_to_topic("reactor/alerts", json_buffer);
}

