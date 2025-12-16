#include "ping.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mqtt_client.h"

static const char *TAG = "telemetry_sender";

static TaskHandle_t s_sender_task = NULL;
static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static volatile bool s_stop_requested = false;
static volatile bool s_mqtt_connected = false;
static SemaphoreHandle_t s_mqtt_connected_sem = NULL;
static mqtt_cmd_callback_t s_cmd_callback = NULL;
static char s_cmd_topic[64] = {0};

// Telemetry state for generating fake data
typedef struct {
    uint32_t sample_id;
    float temperature;
    float accel_mag;
    uint8_t state;       // 0=NORMAL, 1=WARNING, 2=SCRAM
    uint8_t power;
} fake_telemetry_t;

static const char *state_to_string(uint8_t state)
{
    switch (state) {
        case 0: return "NORMAL";
        case 1: return "WARNING";
        case 2: return "SCRAM";
        default: return "UNKNOWN";
    }
}

void set_mqtt_cmd_callback(mqtt_cmd_callback_t cb)
{
    s_cmd_callback = cb;
}

// Generate realistic-looking fake telemetry
static void generate_fake_telemetry(fake_telemetry_t *t)
{
    static uint32_t s_sample_counter = 0;

    t->sample_id = s_sample_counter++;

    // Temperature: oscillates around 45C with some noise (range ~20-80C)
    float base_temp = 45.0f + 20.0f * sinf((float)t->sample_id * 0.05f);
    float noise = ((float)(esp_random() % 1000) / 1000.0f - 0.5f) * 5.0f;
    t->temperature = base_temp + noise;

    // Acceleration: mostly around 9.8 m/s^2 with occasional spikes
    t->accel_mag = 9.81f + ((float)(esp_random() % 100) / 100.0f - 0.5f) * 2.0f;
    if (esp_random() % 20 == 0) {
        // Occasional spike
        t->accel_mag += (float)(esp_random() % 500) / 100.0f;
    }

    // Power: slowly ramps up and down (0-100%)
    t->power = (uint8_t)(50 + 45 * sinf((float)t->sample_id * 0.02f));

    // State: mostly NORMAL, occasionally WARNING, rarely SCRAM
    uint32_t state_rand = esp_random() % 100;
    if (state_rand < 85) {
        t->state = 0;  // NORMAL
    } else if (state_rand < 97) {
        t->state = 1;  // WARNING
    } else {
        t->state = 2;  // SCRAM
    }

    // If temperature is extreme, more likely to be in WARNING/SCRAM
    if (t->temperature > 70.0f) {
        t->state = (t->temperature > 75.0f) ? 2 : 1;
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected to broker");
            s_mqtt_connected = true;
            if (s_mqtt_connected_sem) {
                xSemaphoreGive(s_mqtt_connected_sem);
            }
            // Subscribe to command topic if configured
            if (s_cmd_topic[0] != '\0') {
                int msg_id = esp_mqtt_client_subscribe(s_mqtt_client, s_cmd_topic, 1);
                ESP_LOGI(TAG, "Subscribed to %s (msg_id=%d)", s_cmd_topic, msg_id);
            }
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected from broker");
            s_mqtt_connected = false;
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "MQTT message published, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT data received on topic %.*s", event->topic_len, event->topic);
            if (s_cmd_callback && event->data_len > 0) {
                s_cmd_callback(event->data, event->data_len);
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "TCP transport error");
            }
            break;

        default:
            ESP_LOGD(TAG, "MQTT event: %d", (int)event_id);
            break;
    }
}

typedef struct {
    char broker_uri[128];
    char client_id[64];
    char pub_topic[64];
    uint32_t interval_ms;
    uint32_t count;
} sender_task_params_t;

static void telemetry_sender_task(void *arg)
{
    sender_task_params_t *params = (sender_task_params_t *)arg;
    fake_telemetry_t telemetry;
    uint32_t sent = 0;
    char json_buf[256];

    ESP_LOGI(TAG, "Telemetry sender started: broker=%s, topic=%s, interval=%lums",
             params->broker_uri, params->pub_topic, (unsigned long)params->interval_ms);

    // Initialize MQTT client
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = params->broker_uri,
        .credentials.client_id = params->client_id,
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to init MQTT client");
        goto cleanup;
    }

    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);

    // Wait for connection (with timeout)
    ESP_LOGI(TAG, "Waiting for MQTT connection...");
    if (xSemaphoreTake(s_mqtt_connected_sem, pdMS_TO_TICKS(10000)) != pdTRUE) {
        ESP_LOGE(TAG, "MQTT connection timeout");
        goto cleanup;
    }

    ESP_LOGI(TAG, "MQTT connected, starting telemetry transmission");

    while (!s_stop_requested) {
        if (!s_mqtt_connected) {
            ESP_LOGW(TAG, "MQTT not connected, waiting...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        generate_fake_telemetry(&telemetry);

        // Format JSON payload
        snprintf(json_buf, sizeof(json_buf),
                 "{\"sample_id\":%lu,\"temp\":%.2f,\"accel_mag\":%.3f,\"state\":\"%s\",\"power\":%u}",
                 (unsigned long)telemetry.sample_id,
                 telemetry.temperature,
                 telemetry.accel_mag,
                 state_to_string(telemetry.state),
                 (unsigned)telemetry.power);

        int msg_id = esp_mqtt_client_publish(s_mqtt_client, params->pub_topic,
                                             json_buf, 0, 1, 0);

        if (msg_id >= 0) {
            ESP_LOGI(TAG, "Published to %s: sample=%lu temp=%.1f state=%s power=%u%%",
                     params->pub_topic,
                     (unsigned long)telemetry.sample_id,
                     telemetry.temperature,
                     state_to_string(telemetry.state),
                     (unsigned)telemetry.power);
        } else {
            ESP_LOGW(TAG, "Failed to publish message");
        }

        sent++;
        if (params->count > 0 && sent >= params->count) {
            ESP_LOGI(TAG, "Sent %lu telemetry packets, stopping", (unsigned long)sent);
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(params->interval_ms));
    }

cleanup:
    if (s_mqtt_client) {
        esp_mqtt_client_stop(s_mqtt_client);
        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
    }

    ESP_LOGI(TAG, "Telemetry sender task exiting");
    free(params);
    s_sender_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t start_telemetry_sender(const telemetry_config_t *config)
{
    if (s_sender_task != NULL) {
        ESP_LOGW(TAG, "Telemetry sender already running");
        return ESP_ERR_INVALID_STATE;
    }

    if (config == NULL || config->broker_uri == NULL || config->pub_topic == NULL) {
        ESP_LOGE(TAG, "Invalid config");
        return ESP_ERR_INVALID_ARG;
    }

    // Create semaphore for connection waiting
    if (s_mqtt_connected_sem == NULL) {
        s_mqtt_connected_sem = xSemaphoreCreateBinary();
        if (s_mqtt_connected_sem == NULL) {
            ESP_LOGE(TAG, "Failed to create semaphore");
            return ESP_ERR_NO_MEM;
        }
    }

    sender_task_params_t *params = malloc(sizeof(sender_task_params_t));
    if (params == NULL) {
        ESP_LOGE(TAG, "Failed to allocate task params");
        return ESP_ERR_NO_MEM;
    }

    strncpy(params->broker_uri, config->broker_uri, sizeof(params->broker_uri) - 1);
    params->broker_uri[sizeof(params->broker_uri) - 1] = '\0';

    strncpy(params->client_id, config->client_id ? config->client_id : "esp32_agent",
            sizeof(params->client_id) - 1);
    params->client_id[sizeof(params->client_id) - 1] = '\0';

    strncpy(params->pub_topic, config->pub_topic, sizeof(params->pub_topic) - 1);
    params->pub_topic[sizeof(params->pub_topic) - 1] = '\0';

    // Copy command topic to static storage for subscription on connect
    if (config->cmd_topic) {
        strncpy(s_cmd_topic, config->cmd_topic, sizeof(s_cmd_topic) - 1);
        s_cmd_topic[sizeof(s_cmd_topic) - 1] = '\0';
    } else {
        s_cmd_topic[0] = '\0';
    }

    params->interval_ms = config->interval_ms > 0 ? config->interval_ms : 1000;
    params->count = config->count;

    s_stop_requested = false;
    s_mqtt_connected = false;

    BaseType_t ret = xTaskCreate(
        telemetry_sender_task,
        "telemetry_tx",
        4096,
        params,
        5,
        &s_sender_task
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create telemetry sender task");
        free(params);
        return ESP_FAIL;
    }

    return ESP_OK;
}

void stop_telemetry_sender(void)
{
    if (s_sender_task != NULL) {
        ESP_LOGI(TAG, "Stopping telemetry sender...");
        s_stop_requested = true;
    }
}
