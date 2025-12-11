// ESP32 "reactor" with simple binary protocol over UART (USB)

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

static const char *TAG = "reactor";

// Task configuration
#define CONTROL_TASK_PERIOD_MS     100     // 10 Hz
#define CONTROL_TASK_STACK_SIZE    4096
#define CONTROL_TASK_PRIORITY      5

#define COMMS_TASK_STACK_SIZE      4096
#define COMMS_TASK_PRIORITY        3

// Queues
static QueueHandle_t s_telemetry_queue = NULL;
static QueueHandle_t s_command_queue   = NULL;

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

        // 3) Push telemetry (non-blocking)
        if (xQueueSend(s_telemetry_queue, &t, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Telemetry queue full, dropping sample %u",
                     (unsigned)sample_id);
        }

        sample_id++;

        // 4) Fixed 100 ms period
        vTaskDelayUntil(&last_wake_time,
                        pdMS_TO_TICKS(CONTROL_TASK_PERIOD_MS));
    }
}

// Comms Task (lower priority)
// Drains telemetry queue and sends frames over UART
// Polls UART and feeds bytes into frame parser

static void comms_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Comms task started");

    reactor_telemetry_t t;
    uint8_t rx_buf[64];

    for (;;) {
        // 1) Drain telemetry queue and send frames
        while (xQueueReceive(s_telemetry_queue, &t, 0) == pdTRUE) {
            reactor_comms_send_telemetry(&t);
        }

        // 2) Read any incoming bytes and feed parser
        int len = uart_read_bytes(UART_NUM_0,   // or expose from comms.h
                                  rx_buf,
                                  sizeof(rx_buf),
                                  10 / portTICK_PERIOD_MS);
        if (len > 0) {
            reactor_comms_process_rx_bytes(rx_buf, (uint32_t)len);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 reactor starting up");

    s_telemetry_queue = xQueueCreate(32, sizeof(reactor_telemetry_t));
    s_command_queue   = xQueueCreate(8, sizeof(reactor_command_t));

    if (!s_telemetry_queue || !s_command_queue) {
        ESP_LOGE(TAG, "Failed to create queues");
        return;
    }

    reactor_control_init();
    reactor_comms_init_uart();
    reactor_comms_set_command_queue(s_command_queue);

    // Create control task (higher priority)
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

    // Create comms task (lower priority)
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

    ESP_LOGI(TAG, "Tasks created, system running");
}
