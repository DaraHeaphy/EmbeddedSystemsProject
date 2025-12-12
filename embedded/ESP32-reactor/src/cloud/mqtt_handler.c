// mqtt_handler.c
#include "mqtt_handler.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "MQTT_HANDLER";
static esp_mqtt_client_handle_t client = NULL;
static mqtt_config_t mqtt_cfg;
static bool connected = false;

// Event handler for MQTT events
static void mqtt_event_handler(void* handler_args, esp_event_base_t base, 
                               int32_t event_id, void* event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            connected = true;
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            connected = false;
            break;
            
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
            connected = false;
            break;
            
        default:
            ESP_LOGD(TAG, "Other event id:%d", event->event_id);
            break;
    }
}

esp_err_t mqtt_handler_init(mqtt_config_t* config)
{
    if (!config || !config->broker_uri) {
        ESP_LOGE(TAG, "Invalid config");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Store config
    memcpy(&mqtt_cfg, config, sizeof(mqtt_config_t));
    
    // Setup MQTT client config
    esp_mqtt_client_config_t mqtt_client_cfg = {
        .broker.address.uri = mqtt_cfg.broker_uri,
        .credentials.client_id = mqtt_cfg.client_id,
        .credentials.username = mqtt_cfg.username,
        .credentials.authentication.password = mqtt_cfg.password,
    };
    
    // Create client
    client = esp_mqtt_client_init(&mqtt_client_cfg);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }
    
    // Register event handler
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, 
                                   mqtt_event_handler, NULL);
    
    ESP_LOGI(TAG, "MQTT handler initialized");
    return ESP_OK;
}

esp_err_t mqtt_handler_connect(void)
{
    if (!client) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = esp_mqtt_client_start(client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client");
        return ret;
    }
    
    ESP_LOGI(TAG, "Connecting to MQTT broker...");
    return ESP_OK;
}

esp_err_t mqtt_handler_publish_json(const char* json_data)
{
    if (!mqtt_cfg.default_topic) {
        ESP_LOGE(TAG, "No default topic configured");
        return ESP_ERR_INVALID_ARG;
    }
    
    return mqtt_handler_publish_json_to_topic(mqtt_cfg.default_topic, json_data);
}

esp_err_t mqtt_handler_publish_json_to_topic(const char* topic, const char* json_data)
{
    if (!client || !connected) {
        ESP_LOGE(TAG, "MQTT client not connected");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!topic || !json_data) {
        ESP_LOGE(TAG, "Invalid topic or data");
        return ESP_ERR_INVALID_ARG;
    }
    
    int msg_id = esp_mqtt_client_publish(client, topic, json_data, 
                                         strlen(json_data), 1, 0);
    
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish message");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Published to %s: %s", topic, json_data);
    return ESP_OK;
}

bool mqtt_handler_is_connected(void)
{
    return connected;
}

void mqtt_handler_cleanup(void)
{
    if (client) {
        esp_mqtt_client_stop(client);
        esp_mqtt_client_destroy(client);
        client = NULL;
        connected = false;
        ESP_LOGI(TAG, "MQTT handler cleaned up");
    }
}

// Example usage in main.c:
/*
#include "mqtt_handler.h"

void app_main(void)
{
    // Initialize WiFi first (not shown)
    
    // Configure MQTT
    mqtt_config_t config = {
        .broker_uri = "mqtt://broker.hivemq.com",
        .client_id = "reactor_core_001",
        .username = NULL,  // Set if needed
        .password = NULL,  // Set if needed
        .default_topic = "reactor/sensors"
    };
    
    // Init and connect
    mqtt_handler_init(&config);
    mqtt_handler_connect();
    
    // Wait for connection
    while (!mqtt_handler_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Publish sensor data
    const char* json = "{\"temp\":72.5,\"accel_x\":0.12,\"accel_y\":0.05,\"accel_z\":9.81}";
    mqtt_handler_publish_json(json);
    
    // Or publish to specific topic
    mqtt_handler_publish_json_to_topic("reactor/alerts", "{\"status\":\"critical\"}");
}
*/