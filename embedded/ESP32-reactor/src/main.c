#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_log.h"

#include "reactor_control.h"
#include "reactor_comms.h"

static const char *TAG = "reactor";

#define CONTROL_PERIOD_MS   100
#define CONTROL_STACK       4096
#define CONTROL_PRIORITY    5

#define COMMS_STACK         4096
#define COMMS_PRIORITY      3

static QueueHandle_t s_telemetry_queue = NULL;
static QueueHandle_t s_command_queue = NULL;

// high priority control loop, runs at 10hz
static void control_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "control task started");

    TickType_t last_wake = xTaskGetTickCount();
    uint32_t sample_id = 0;

    for (;;) {
        // process any pending commands
        reactor_command_t cmd;
        while (xQueueReceive(s_command_queue, &cmd, 0) == pdTRUE) {
            reactor_control_handle_command(&cmd);
        }

        // run control step
        reactor_telemetry_t t;
        reactor_control_step(sample_id, &t);

        // queue telemetry for transmission
        if (xQueueSend(s_telemetry_queue, &t, 0) != pdTRUE) {
            ESP_LOGW(TAG, "telemetry queue full, dropping sample %lu", (unsigned long)sample_id);
        }

        sample_id++;
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    }
}

// lower priority comms task, handles uart tx/rx
static void comms_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "comms task started");

    reactor_telemetry_t t;
    uint8_t rx_buf[64];

    for (;;) {
        // drain telemetry queue and send
        while (xQueueReceive(s_telemetry_queue, &t, 0) == pdTRUE) {
            comms_send_telemetry(&t);
        }

        // read incoming bytes and process
        int len = uart_read_bytes(COMMS_UART_NUM, rx_buf, sizeof(rx_buf),
                                  pdMS_TO_TICKS(10));
        if (len > 0) {
            comms_process_rx(rx_buf, (uint32_t)len);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "reactor starting");

    s_telemetry_queue = xQueueCreate(32, sizeof(reactor_telemetry_t));
    s_command_queue = xQueueCreate(8, sizeof(reactor_command_t));

    if (!s_telemetry_queue || !s_command_queue) {
        ESP_LOGE(TAG, "failed to create queues");
        return;
    }

    reactor_control_init();
    comms_init();
    comms_set_command_queue(s_command_queue);

    if (xTaskCreate(control_task, "control", CONTROL_STACK, NULL, CONTROL_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create control task");
        return;
    }

    if (xTaskCreate(comms_task, "comms", COMMS_STACK, NULL, COMMS_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create comms task");
        return;
    }

    ESP_LOGI(TAG, "running");
}
