#include "mqtt.h"

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "mqtt_client.h"
#include "protocol.h"

static const char *TAG = "mqtt";

static TaskHandle_t s_task = NULL;
static esp_mqtt_client_handle_t s_client = NULL;
static volatile bool s_stop = false;
static volatile bool s_connected = false;
static SemaphoreHandle_t s_connect_sem = NULL;
static QueueHandle_t s_telemetry_queue = NULL;
static mqtt_command_callback_t s_cmd_callback = NULL;
static char s_cmd_topic[64] = {0};

static const char *state_str(uint8_t s)
{
    switch (s) {
    case 0:  return "NORMAL";
    case 1:  return "WARNING";
    case 2:  return "SCRAM";
    default: return "UNKNOWN";
    }
}

static void build_client_id(char *out, size_t len, const char *base)
{
    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        snprintf(out, len, "%s_%02X%02X%02X%02X%02X%02X",
                 base ? base : "esp32", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        snprintf(out, len, "%s", base ? base : "esp32");
    }
}

void mqtt_set_command_callback(mqtt_command_callback_t cb)
{
    s_cmd_callback = cb;
}

esp_err_t mqtt_update_telemetry(const mqtt_telemetry_t *telemetry)
{
    if (!telemetry || !s_telemetry_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    return xQueueOverwrite(s_telemetry_queue, telemetry) == pdPASS ? ESP_OK : ESP_FAIL;
}

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    esp_mqtt_event_handle_t event = data;

    switch (id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected");
        s_connected = true;
        if (s_connect_sem) {
            xSemaphoreGive(s_connect_sem);
        }
        if (s_cmd_topic[0]) {
            esp_mqtt_client_subscribe(s_client, s_cmd_topic, 1);
            ESP_LOGI(TAG, "subscribed to %s", s_cmd_topic);
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected");
        s_connected = false;
        break;

    case MQTT_EVENT_DATA:
        if (s_cmd_callback && event->data_len > 0) {
            s_cmd_callback(event->data, event->data_len);
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "error");
        break;

    default:
        break;
    }
}

typedef struct {
    char broker_uri[128];
    char client_id[64];
    char pub_topic[64];
    uint32_t interval_ms;
} task_params_t;

static void mqtt_task(void *arg)
{
    task_params_t *params = (task_params_t *)arg;
    uint32_t last_sample_id = UINT32_MAX;
    char json[256];

    ESP_LOGI(TAG, "starting: broker=%s topic=%s", params->broker_uri, params->pub_topic);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = params->broker_uri,
        .credentials.client_id = params->client_id,
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "failed to init client");
        goto cleanup;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    if (esp_mqtt_client_start(s_client) != ESP_OK) {
        ESP_LOGE(TAG, "failed to start client");
        goto cleanup;
    }

    if (xSemaphoreTake(s_connect_sem, pdMS_TO_TICKS(10000)) != pdTRUE) {
        ESP_LOGE(TAG, "connection timeout");
        goto cleanup;
    }

    ESP_LOGI(TAG, "ready, publishing telemetry");

    while (!s_stop) {
        if (!s_connected) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        mqtt_telemetry_t t;
        if (xQueuePeek(s_telemetry_queue, &t, 0) != pdTRUE) {
            vTaskDelay(pdMS_TO_TICKS(params->interval_ms));
            continue;
        }

        // skip if we already published this sample
        if (t.sample_id == last_sample_id) {
            vTaskDelay(pdMS_TO_TICKS(params->interval_ms));
            continue;
        }

        snprintf(json, sizeof(json),
                 "{\"sample_id\":%lu,\"temp\":%.2f,\"accel_mag\":%.3f,\"state\":\"%s\",\"power\":%u}",
                 (unsigned long)t.sample_id, t.temp_c, t.accel_mag,
                 state_str(t.state), (unsigned)t.power);

        int msg_id = esp_mqtt_client_publish(s_client, params->pub_topic, json, 0, 1, 0);
        if (msg_id >= 0) {
            ESP_LOGI(TAG, "pub: id=%lu temp=%.1f state=%s",
                     (unsigned long)t.sample_id, t.temp_c, state_str(t.state));
        }

        last_sample_id = t.sample_id;
        vTaskDelay(pdMS_TO_TICKS(params->interval_ms));
    }

cleanup:
    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    free(params);
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t mqtt_start(const mqtt_config_t *config)
{
    if (s_task) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!config || !config->broker_uri || !config->pub_topic) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_connect_sem) {
        s_connect_sem = xSemaphoreCreateBinary();
    }

    if (!s_telemetry_queue) {
        s_telemetry_queue = xQueueCreate(1, sizeof(mqtt_telemetry_t));
    } else {
        xQueueReset(s_telemetry_queue);
    }

    if (config->cmd_topic) {
        strncpy(s_cmd_topic, config->cmd_topic, sizeof(s_cmd_topic) - 1);
    }

    task_params_t *params = malloc(sizeof(task_params_t));
    if (!params) {
        return ESP_ERR_NO_MEM;
    }

    strncpy(params->broker_uri, config->broker_uri, sizeof(params->broker_uri) - 1);
    strncpy(params->pub_topic, config->pub_topic, sizeof(params->pub_topic) - 1);
    build_client_id(params->client_id, sizeof(params->client_id), config->client_id);
    params->interval_ms = config->interval_ms > 0 ? config->interval_ms : 1000;

    s_stop = false;
    s_connected = false;
    xSemaphoreTake(s_connect_sem, 0);

    if (xTaskCreate(mqtt_task, "mqtt", 4096, params, 5, &s_task) != pdPASS) {
        free(params);
        return ESP_FAIL;
    }

    return ESP_OK;
}

void mqtt_stop(void)
{
    if (s_task) {
        s_stop = true;
    }
}
