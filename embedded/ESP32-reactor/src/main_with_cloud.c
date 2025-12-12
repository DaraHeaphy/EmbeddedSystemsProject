// ESP32 "reactor" with UART protocol + Cloud MQTT publishing

// Standard headers
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

// FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// ESP-IDF drivers
#include "driver/uart.h"
#include "esp_log.h"

#include "reactor_control.h"
#include "reactor_comms.h"

// Cloud components
#include "cloud/wifi_manager.h"
#include "cloud/mqtt_handler.h"
#include "cloud/cloud_publisher.h"

static const char *TAG = "reactor";

// Task configuration
#define CONTROL_TASK_PERIOD_MS     100     // 10 Hz
#define CONTROL_TASK_STACK_SIZE    4096
#define CONTROL_TASK_PRIORITY      5

#define COMMS_TASK_STACK_SIZE      4096
#define COMMS_TASK_PRIORITY        3

#define CLOUD_TASK_STACK_SIZE      4096
#define CLOUD_TASK_PRIORITY        2       // Lower than comms

// Queues
static QueueHandle_t s_telemetry_queue = NULL;
static QueueHandle_t s_command_queue   = NULL;
static QueueHandle_t s_cloud_queue     = NULL;  // Queue for cloud publishing

// ============================================================================
// WiFi & MQTT Configuration - CHANGE THESE FOR YOUR NETWORK!
// ============================================================================
#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"
#define MQTT_BROKER_URI "mqtt://192.168.1.100:1883"  // Your PC's IP
#define MQTT_CLIENT_ID  "reactor_core_001"
#define MQTT_TOPIC      "reactor/telemetry"

// Control Task (high priority, deterministic)
static void control_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Control task started");

    TickType_t last_wake_time = xTaskGetTickCount();
    uint32_t sample_id = 0;

    for (;;) {
        // 1) Consume any pending commands
        reactor_command_t cmd;
        while (xQueueReceive(s_command_queue, &cmd, 0) == pdTRUE) {
            reactor_control_handle_command(&cmd);
        }

        // 2) Run one control step and get telemetry
        reactor_telemetry_t t;
        reactor_control_step(sample_id, &t);

        // 3) Push telemetry to UART comms queue (non-blocking)
        if (xQueueSend(s_telemetry_queue, &t, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Telemetry queue full, dropping sample %u",
                     (unsigned)sample_id);
        }

        // 4) Push telemetry to cloud queue (non-blocking)
        //    Only send every 10th sample to reduce cloud traffic
        if (sample_id % 10 == 0) {
            if (xQueueSend(s_cloud_queue, &t, 0) != pdTRUE) {
                ESP_LOGD(TAG, "Cloud queue full, dropping sample %u",
                         (unsigned)sample_id);
            }
        }

        sample_id++;

        // 5) Fixed 100 ms period
        vTaskDelayUntil(&last_wake_time,
                        pdMS_TO_TICKS(CONTROL_TASK_PERIOD_MS));
    }
}

// Comms Task (UART communication)
static void comms_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Comms task started");

    reactor_telemetry_t t;
    uint8_t rx_buf[64];

    for (;;) {
        // 1) Drain telemetry queue and send frames over UART
        while (xQueueReceive(s_telemetry_queue, &t, 0) == pdTRUE) {
            reactor_comms_send_telemetry(&t);
        }

        // 2) Read any incoming bytes and feed parser
        int len = uart_read_bytes(UART_NUM_0,
                                  rx_buf,
                                  sizeof(rx_buf),
                                  10 / portTICK_PERIOD_MS);
        if (len > 0) {
            reactor_comms_process_rx_bytes(rx_buf, (uint32_t)len);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Cloud Task (MQTT publishing)
static void cloud_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Cloud task started");

    reactor_telemetry_t t;
    uint32_t publish_count = 0;

    for (;;) {
        // 1) Drain cloud queue and publish to MQTT
        while (xQueueReceive(s_cloud_queue, &t, 0) == pdTRUE) {
            esp_err_t err = cloud_publisher_publish_telemetry(&t);
            if (err == ESP_OK) {
                publish_count++;
                if (publish_count % 10 == 0) {
                    ESP_LOGI(TAG, "Published %lu telemetry messages to cloud",
                             (unsigned long)publish_count);
                }
            }
        }

        // 2) Sleep a bit to avoid busy loop
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 reactor starting up");

    // Create queues
    s_telemetry_queue = xQueueCreate(32, sizeof(reactor_telemetry_t));
    s_command_queue   = xQueueCreate(8, sizeof(reactor_command_t));
    s_cloud_queue     = xQueueCreate(16, sizeof(reactor_telemetry_t));

    if (!s_telemetry_queue || !s_command_queue || !s_cloud_queue) {
        ESP_LOGE(TAG, "Failed to create queues");
        return;
    }

    // Initialize reactor control and comms
    reactor_control_init();
    reactor_comms_init_uart();
    reactor_comms_set_command_queue(s_command_queue);

    // Initialize WiFi
    ESP_LOGI(TAG, "Initializing WiFi...");
    wifi_config_simple_t wifi_config = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASSWORD
    };
    
    esp_err_t err = wifi_manager_init(&wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(err));
        ESP_LOGW(TAG, "Continuing without cloud connection...");
    }

    // Wait for WiFi connection
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    int wifi_wait_count = 0;
    while (!wifi_manager_is_connected() && wifi_wait_count < 50) {
        vTaskDelay(pdMS_TO_TICKS(200));
        wifi_wait_count++;
    }

    if (wifi_manager_is_connected()) {
        ESP_LOGI(TAG, "WiFi connected! Initializing MQTT...");
        
        // Initialize MQTT
        mqtt_config_t mqtt_config = {
            .broker_uri = MQTT_BROKER_URI,
            .client_id = MQTT_CLIENT_ID,
            .username = NULL,
            .password = NULL,
            .default_topic = MQTT_TOPIC
        };

        err = mqtt_handler_init(&mqtt_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize MQTT: %s", esp_err_to_name(err));
        } else {
            err = mqtt_handler_connect();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to connect to MQTT broker: %s", 
                         esp_err_to_name(err));
            } else {
                ESP_LOGI(TAG, "MQTT initialization started");
            }
        }
    } else {
        ESP_LOGW(TAG, "WiFi not connected, cloud features disabled");
    }

    // Create control task (highest priority)
    BaseType_t ok = xTaskCreate(
        control_task,
        "ControlTask",
        CONTROL_TASK_STACK_SIZE,
        NULL,
        CONTROL_TASK_PRIORITY,
        NULL
    );
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create ControlTask");
        return;
    }

    // Create comms task (medium priority)
    ok = xTaskCreate(
        comms_task,
        "CommsTask",
        COMMS_TASK_STACK_SIZE,
        NULL,
        COMMS_TASK_PRIORITY,
        NULL
    );
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create CommsTask");
        return;
    }

    // Create cloud task (lowest priority)
    ok = xTaskCreate(
        cloud_task,
        "CloudTask",
        CLOUD_TASK_STACK_SIZE,
        NULL,
        CLOUD_TASK_PRIORITY,
        NULL
    );
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create CloudTask");
        return;
    }

    ESP_LOGI(TAG, "All tasks created, system running");
}

