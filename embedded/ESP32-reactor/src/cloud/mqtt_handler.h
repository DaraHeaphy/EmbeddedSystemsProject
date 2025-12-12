// mqtt_handler.h
#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include "mqtt_client.h"
#include <stdbool.h>

// Configuration structure
typedef struct {
    const char* broker_uri;
    const char* client_id;
    const char* username;
    const char* password;
    const char* default_topic;
} mqtt_config_t;

// Initialize MQTT client with config
esp_err_t mqtt_handler_init(mqtt_config_t* config);

// Connect to broker
esp_err_t mqtt_handler_connect(void);

// Publish JSON data to default topic
esp_err_t mqtt_handler_publish_json(const char* json_data);

// Publish JSON data to specific topic
esp_err_t mqtt_handler_publish_json_to_topic(const char* topic, const char* json_data);

// Check connection status
bool mqtt_handler_is_connected(void);

// Disconnect and cleanup
void mqtt_handler_cleanup(void);

#endif // MQTT_HANDLER_H