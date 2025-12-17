#include "ping.h"

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "mqtt_client.h"

static const char *TAG = "telemetry_sender";

static TaskHandle_t s_sender_task = NULL;
static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static volatile bool s_stop_requested = false;
static volatile bool s_mqtt_connected = false;
static SemaphoreHandle_t s_mqtt_connected_sem = NULL;
static mqtt_cmd_callback_t s_cmd_callback = NULL;
static char s_cmd_topic[64] = {0};
static QueueHandle_t s_telemetry_queue = NULL;

static const char *state_to_string(uint8_t state)
{
    switch (state) {
        case 0: return "NORMAL";
        case 1: return "WARNING";
        case 2: return "SCRAM";
        default: return "UNKNOWN";
    }
}

static void build_unique_client_id(char *out, size_t out_len, const char *base)
{
    if (out == NULL || out_len == 0) {
        return;
    }

    if (base == NULL || base[0] == '\0') {
        base = "esp32_agent";
    }

    uint8_t mac[6] = {0};
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read WiFi STA MAC for client_id suffix: %s", esp_err_to_name(err));
        snprintf(out, out_len, "%s", base);
        return;
    }

    int written = snprintf(out, out_len, "%s_%02X%02X%02X%02X%02X%02X",
                           base, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    if (written < 0 || (size_t)written >= out_len) {
        ESP_LOGW(TAG, "client_id truncated; increase buffer or shorten base id");
        snprintf(out, out_len, "%s", base);
    }
}

void set_mqtt_cmd_callback(mqtt_cmd_callback_t cb)
{
    s_cmd_callback = cb;
}

esp_err_t telemetry_sender_update(const reactor_telemetry_t *telemetry)
{
    if (telemetry == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_telemetry_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return (xQueueOverwrite(s_telemetry_queue, telemetry) == pdPASS) ? ESP_OK : ESP_FAIL;
}

static const char *mqtt_event_name(int32_t event_id)
{
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_BEFORE_CONNECT: return "BEFORE_CONNECT";
        case MQTT_EVENT_CONNECTED: return "CONNECTED";
        case MQTT_EVENT_DISCONNECTED: return "DISCONNECTED";
        case MQTT_EVENT_SUBSCRIBED: return "SUBSCRIBED";
        case MQTT_EVENT_UNSUBSCRIBED: return "UNSUBSCRIBED";
        case MQTT_EVENT_PUBLISHED: return "PUBLISHED";
        case MQTT_EVENT_DATA: return "DATA";
        case MQTT_EVENT_ERROR: return "ERROR";
        case MQTT_EVENT_DELETED: return "DELETED";
        default: return "UNKNOWN";
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t event = event_data;
    if (event == NULL && (esp_mqtt_event_id_t)event_id != MQTT_EVENT_BEFORE_CONNECT) {
        ESP_LOGE(TAG, "[MQTT] event=%s but event_data is NULL", mqtt_event_name(event_id));
        return;
    }

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_BEFORE_CONNECT:
            ESP_LOGI(TAG, "[MQTT] event=%s", mqtt_event_name(event_id));
            break;

        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "[MQTT] event=%s", mqtt_event_name(event_id));
            ESP_LOGI(TAG, "[MQTT] connected");
            s_mqtt_connected = true;
            if (s_mqtt_connected_sem) {
                xSemaphoreGive(s_mqtt_connected_sem);
            }
            // Subscribe to command topic if configured
            if (s_cmd_topic[0] != '\0') {
                int msg_id = esp_mqtt_client_subscribe(s_mqtt_client, s_cmd_topic, 1);
                if (msg_id >= 0) {
                    ESP_LOGI(TAG, "[MQTT] subscribe queued: topic=%s qos=1 msg_id=%d", s_cmd_topic, msg_id);
                } else {
                    ESP_LOGE(TAG, "[MQTT] subscribe failed: topic=%s", s_cmd_topic);
                }
            }
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "[MQTT] event=%s", mqtt_event_name(event_id));
            ESP_LOGW(TAG, "[MQTT] disconnected");
            s_mqtt_connected = false;
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "[MQTT] event=%s msg_id=%d", mqtt_event_name(event_id), event ? event->msg_id : -1);
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "[MQTT] event=%s msg_id=%d", mqtt_event_name(event_id), event ? event->msg_id : -1);
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "[MQTT] event=%s msg_id=%d", mqtt_event_name(event_id), event ? event->msg_id : -1);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "[MQTT] event=%s topic=%.*s bytes=%d", mqtt_event_name(event_id),
                     event->topic_len, event->topic, event->data_len);
            if (s_cmd_callback && event->data_len > 0) {
                s_cmd_callback(event->data, event->data_len);
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "[MQTT] event=%s", mqtt_event_name(event_id));
            if (event && event->error_handle) {
                const esp_mqtt_error_codes_t *e = event->error_handle;
                ESP_LOGE(TAG, "[MQTT] error_type=%d connect_rc=%d tls_last_err=%d tls_stack_err=%d sock_errno=%d (%s)",
                         (int)e->error_type,
                         (int)e->connect_return_code,
                         (int)e->esp_tls_last_esp_err,
                         (int)e->esp_tls_stack_err,
                         (int)e->esp_transport_sock_errno,
                         strerror(e->esp_transport_sock_errno));
            } else {
                ESP_LOGE(TAG, "[MQTT] error without details (null error_handle)");
            }
            break;

        default:
            ESP_LOGI(TAG, "[MQTT] event=%s(%d)", mqtt_event_name(event_id), (int)event_id);
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
    uint32_t sent = 0;
    char json_buf[256];
    uint32_t last_published_sample_id = UINT32_MAX;
    bool warned_no_telemetry = false;

    ESP_LOGI(TAG, "[1/6] telemetry task start");
    ESP_LOGI(TAG, "[2/6] broker=%s", params->broker_uri);
    ESP_LOGI(TAG, "[3/6] client_id=%s", params->client_id);
    ESP_LOGI(TAG, "[4/6] pub_topic=%s interval=%lums count=%lu qos=1",
             params->pub_topic, (unsigned long)params->interval_ms, (unsigned long)params->count);
    ESP_LOGI(TAG, "[5/6] cmd_topic=%s", (s_cmd_topic[0] != '\0') ? s_cmd_topic : "(disabled)");

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
    esp_err_t start_err = esp_mqtt_client_start(s_mqtt_client);
    if (start_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(start_err));
        goto cleanup;
    }

    // Wait for connection (with timeout)
    ESP_LOGI(TAG, "[6/6] waiting for MQTT_EVENT_CONNECTED (timeout=10s)...");
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

        reactor_telemetry_t telemetry;
        if (s_telemetry_queue == NULL ||
            xQueuePeek(s_telemetry_queue, &telemetry, 0) != pdTRUE) {
            if (!warned_no_telemetry) {
                ESP_LOGW(TAG, "No reactor telemetry received yet; waiting for UART frames...");
                warned_no_telemetry = true;
            }
            vTaskDelay(pdMS_TO_TICKS(params->interval_ms));
            continue;
        }
        warned_no_telemetry = false;
        if (telemetry.sample_id == last_published_sample_id) {
            vTaskDelay(pdMS_TO_TICKS(params->interval_ms));
            continue;
        }

        // Format JSON payload
        snprintf(json_buf, sizeof(json_buf),
                 "{\"sample_id\":%lu,\"temp\":%.2f,\"accel_mag\":%.3f,\"state\":\"%s\",\"power\":%u}",
                 (unsigned long)telemetry.sample_id,
                 telemetry.temp_c,
                 telemetry.accel_mag,
                 state_to_string(telemetry.state),
                 (unsigned)telemetry.power);

        int msg_id = esp_mqtt_client_publish(s_mqtt_client, params->pub_topic,
                                             json_buf, 0, 1, 0);

        if (msg_id >= 0) {
            ESP_LOGI(TAG, "Publish queued: topic=%s msg_id=%d sample=%lu temp=%.1f state=%s power=%u%%",
                     params->pub_topic,
                     msg_id,
                     (unsigned long)telemetry.sample_id,
                     telemetry.temp_c,
                     state_to_string(telemetry.state),
                     (unsigned)telemetry.power);
        } else {
            ESP_LOGE(TAG, "Publish failed (msg_id=%d). Check MQTT_EVENT_ERROR logs.", msg_id);
        }

        last_published_sample_id = telemetry.sample_id;
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

    if (s_telemetry_queue == NULL) {
        s_telemetry_queue = xQueueCreate(1, sizeof(reactor_telemetry_t));
        if (s_telemetry_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create telemetry queue");
            return ESP_ERR_NO_MEM;
        }
    } else {
        (void)xQueueReset(s_telemetry_queue);
    }

    sender_task_params_t *params = malloc(sizeof(sender_task_params_t));
    if (params == NULL) {
        ESP_LOGE(TAG, "Failed to allocate task params");
        return ESP_ERR_NO_MEM;
    }

    strncpy(params->broker_uri, config->broker_uri, sizeof(params->broker_uri) - 1);
    params->broker_uri[sizeof(params->broker_uri) - 1] = '\0';

    build_unique_client_id(params->client_id, sizeof(params->client_id), config->client_id);

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
    // Ensure the "connected" semaphore starts empty so the task truly waits for MQTT_EVENT_CONNECTED
    (void)xSemaphoreTake(s_mqtt_connected_sem, 0);

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
