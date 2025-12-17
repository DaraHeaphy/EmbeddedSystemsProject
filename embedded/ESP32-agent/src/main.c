#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "protocol.h"
#include "frame_parser.h"
#include "wifi.h"
#include "mqtt.h"
#include "cJSON.h"

#define UART_LINK       UART_NUM_2
#define LINK_TX_PIN     GPIO_NUM_17
#define LINK_RX_PIN     GPIO_NUM_16
#define BAUD_RATE       115200
#define RX_BUF_SIZE     1024

static const char *TAG = "agent";

static const char *state_str(uint8_t s)
{
    switch (s) {
    case 0:  return "NORMAL";
    case 1:  return "WARNING";
    case 2:  return "SCRAM";
    default: return "UNKNOWN";
    }
}

// sends a framed command to the reactor over uart
static void send_command(uint8_t cmd_id, const uint8_t *extra, uint8_t extra_len)
{
    uint8_t payload[1 + extra_len];
    payload[0] = cmd_id;
    if (extra && extra_len > 0) {
        memcpy(&payload[1], extra, extra_len);
    }

    uint8_t header[3] = { FRAME_START_BYTE, MSG_TYPE_COMMAND, (uint8_t)(1 + extra_len) };
    uint8_t checksum = protocol_calc_checksum(MSG_TYPE_COMMAND, 1 + extra_len, payload);

    uart_write_bytes(UART_LINK, (const char *)header, 3);
    uart_write_bytes(UART_LINK, (const char *)payload, 1 + extra_len);
    uart_write_bytes(UART_LINK, (const char *)&checksum, 1);
}

static void send_scram(void)
{
    send_command(CMD_ID_SCRAM, NULL, 0);
    ESP_LOGI(TAG, "sent SCRAM");
}

static void send_reset_normal(void)
{
    send_command(CMD_ID_RESET_NORMAL, NULL, 0);
    ESP_LOGI(TAG, "sent RESET_NORMAL");
}

static void send_set_power(int32_t value)
{
    send_command(CMD_ID_SET_POWER, (const uint8_t *)&value, sizeof(value));
    ESP_LOGI(TAG, "sent SET_POWER=%" PRId32, value);
}

// handles incoming mqtt commands and forwards them to the reactor
static void handle_mqtt_command(const char *data, int data_len)
{
    cJSON *root = cJSON_ParseWithLength(data, data_len);
    if (!root) {
        ESP_LOGW(TAG, "failed to parse command json");
        return;
    }

    cJSON *cmd = cJSON_GetObjectItem(root, "command");
    if (!cJSON_IsString(cmd)) {
        ESP_LOGW(TAG, "missing command field");
        cJSON_Delete(root);
        return;
    }

    const char *cmd_str = cmd->valuestring;

    if (strcmp(cmd_str, "SCRAM") == 0) {
        send_scram();
    } else if (strcmp(cmd_str, "RESET_NORMAL") == 0) {
        send_reset_normal();
    } else if (strcmp(cmd_str, "SET_POWER") == 0) {
        cJSON *val = cJSON_GetObjectItem(root, "value");
        int32_t power = cJSON_IsNumber(val) ? (int32_t)val->valueint : 50;
        send_set_power(power);
    } else {
        ESP_LOGW(TAG, "unknown command: %s", cmd_str);
    }

    cJSON_Delete(root);
}

// handles telemetry frames from the reactor and queues them for mqtt
static void handle_telemetry(const uint8_t *payload, uint8_t len)
{
    if (len != TELEMETRY_PAYLOAD_LEN) {
        ESP_LOGW(TAG, "bad telemetry len: %u", (unsigned)len);
        return;
    }

    uint32_t sample_id;
    float temp_c, accel_mag;
    uint8_t state, power;

    memcpy(&sample_id, payload, sizeof(sample_id));
    memcpy(&temp_c, payload + 4, sizeof(temp_c));
    memcpy(&accel_mag, payload + 8, sizeof(accel_mag));
    state = payload[12];
    power = payload[13];

    mqtt_telemetry_t t = {
        .sample_id = sample_id,
        .temp_c = temp_c,
        .accel_mag = accel_mag,
        .state = state,
        .power = power,
    };
    mqtt_update_telemetry(&t);

    ESP_LOGI(TAG, "rx: id=%" PRIu32 " temp=%.1fC accel=%.2fg state=%s power=%u%%",
             sample_id, temp_c, accel_mag, state_str(state), (unsigned)power);
}

// callback invoked by frame parser when a valid frame arrives
static void on_frame(void *ctx, uint8_t msg_type, const uint8_t *payload, uint8_t len)
{
    (void)ctx;
    if (msg_type == MSG_TYPE_TELEMETRY) {
        handle_telemetry(payload, len);
    } else {
        ESP_LOGW(TAG, "unhandled msg_type=0x%02X", (unsigned)msg_type);
    }
}

static void init_uart(void)
{
    uart_config_t cfg = {
        .baud_rate = BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
#ifdef UART_SCLK_APB
        .source_clk = UART_SCLK_APB,
#endif
    };

    ESP_ERROR_CHECK(uart_param_config(UART_LINK, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_LINK, LINK_TX_PIN, LINK_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_LINK, RX_BUF_SIZE, 0, 0, NULL, 0));
    uart_flush_input(UART_LINK);

    ESP_LOGI(TAG, "uart ready: baud=%d rx=GPIO%d tx=GPIO%d",
             BAUD_RATE, (int)LINK_RX_PIN, (int)LINK_TX_PIN);
}

// task that reads from uart and feeds the frame parser
static void uart_rx_task(void *arg)
{
    (void)arg;
    frame_parser_t parser;
    frame_parser_init(&parser, on_frame, NULL);

    uint8_t buf[128];
    for (;;) {
        int n = uart_read_bytes(UART_LINK, buf, sizeof(buf), pdMS_TO_TICKS(50));
        if (n > 0) {
            frame_parser_feed(&parser, buf, (size_t)n);
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "agent starting");

    esp_err_t wifi_ret = wifi_init_sta();
    if (wifi_ret == ESP_OK) {
        ESP_LOGI(TAG, "wifi connected, starting mqtt");

        mqtt_set_command_callback(handle_mqtt_command);

        mqtt_config_t cfg = {
            .broker_uri = "mqtt://alderaan.software-engineering.ie:1883",
            .client_id = "reactor_bridge_agent",
            .pub_topic = "reactor/sensors",
            .cmd_topic = "reactor/commands",
            .interval_ms = 1000,
        };
        mqtt_start(&cfg);
    } else {
        ESP_LOGW(TAG, "wifi failed, skipping mqtt");
    }

    init_uart();
    xTaskCreate(uart_rx_task, "uart_rx", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "listening for telemetry");
}
