#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"

#include "wifi.h"
#include "ping.h"
#include "cJSON.h"

// ---------- protocol ----------
#define FRAME_START_BYTE       0xAA

#define MSG_TYPE_TELEMETRY     0x01
#define MSG_TYPE_COMMAND       0x10

#define MAX_PAYLOAD_LEN        64

// command IDs (must match reactor side)
#define CMD_ID_SCRAM           1
#define CMD_ID_RESET_NORMAL    2
#define CMD_ID_SET_POWER       3

// telemetry payload: <u32 sample_id><float temp_c><float accel_mag><u8 state><u8 power>
#define TELEMETRY_LEN          14

static uint8_t calc_checksum(uint8_t msg_type, uint8_t length, const uint8_t *payload)
{
    uint8_t c = (uint8_t)(msg_type ^ length);
    for (uint8_t i = 0; i < length; i++) c ^= payload[i];
    return c;
}

// ---------- UART wiring ----------
#define UART_USB_LOGS          UART_NUM_0   // USB serial monitor / flashing
#define UART_LINK             UART_NUM_2   // wired link to reactor

// On both boards: TX=GPIO17, RX=GPIO16 (as you described)
#define LINK_TX_PIN            GPIO_NUM_17  // Agent TX (wire to Reactor RX)
#define LINK_RX_PIN            GPIO_NUM_16  // Agent RX (wire from Reactor TX)

#define BAUD_RATE              115200
#define RX_BUF_BYTES           1024

static const char *TAG = "agent";

// ---------- frame parser ----------
typedef enum {
    FP_WAIT_START = 0,
    FP_READ_TYPE,
    FP_READ_LEN,
    FP_READ_PAYLOAD,
    FP_READ_CHECKSUM
} fp_state_t;

typedef struct {
    fp_state_t state;
    uint8_t msg_type;
    uint8_t len;
    uint8_t payload[MAX_PAYLOAD_LEN];
    uint8_t idx;
    uint8_t checksum;
} frame_parser_t;

static void fp_reset(frame_parser_t *p)
{
    p->state = FP_WAIT_START;
    p->msg_type = 0;
    p->len = 0;
    p->idx = 0;
    p->checksum = 0;
}

static const char *state_name(uint8_t s)
{
    switch (s) {
        case 0: return "NORMAL";
        case 1: return "WARNING";
        case 2: return "SCRAM";
        default: return "UNKNOWN";
    }
}

// ---------- NEW: send frames to reactor ----------
static void send_frame_uart(uart_port_t uart, uint8_t msg_type, const uint8_t *payload, uint8_t len)
{
    uint8_t header[3] = { FRAME_START_BYTE, msg_type, len };
    uint8_t c = calc_checksum(msg_type, len, payload);

    uart_write_bytes(uart, (const char *)header, 3);
    if (len && payload) uart_write_bytes(uart, (const char *)payload, len);
    uart_write_bytes(uart, (const char *)&c, 1);
}

static void agent_send_reset_normal(void)
{
    uint8_t payload[1] = { CMD_ID_RESET_NORMAL };
    send_frame_uart(UART_LINK, MSG_TYPE_COMMAND, payload, (uint8_t)sizeof(payload));
    ESP_LOGI(TAG, "sent RESET_NORMAL");
}

// (optional) other hardcoded commands you might want later:
static void agent_send_scram(void)
{
    uint8_t payload[1] = { CMD_ID_SCRAM };
    send_frame_uart(UART_LINK, MSG_TYPE_COMMAND, payload, (uint8_t)sizeof(payload));
    ESP_LOGI(TAG, "sent SCRAM");
}

static void agent_send_set_power(int32_t value)
{
    uint8_t payload[1 + 4];
    payload[0] = CMD_ID_SET_POWER;
    memcpy(&payload[1], &value, sizeof(value)); // little-endian on ESP32
    send_frame_uart(UART_LINK, MSG_TYPE_COMMAND, payload, (uint8_t)sizeof(payload));
    ESP_LOGI(TAG, "sent SET_POWER=%" PRId32, value);
}

// ---------- MQTT command handler ----------
static void handle_mqtt_command(const char *data, int data_len)
{
    cJSON *root = cJSON_ParseWithLength(data, data_len);
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse command JSON");
        return;
    }

    cJSON *cmd = cJSON_GetObjectItem(root, "command");
    if (!cJSON_IsString(cmd)) {
        ESP_LOGW(TAG, "Missing or invalid 'command' field");
        cJSON_Delete(root);
        return;
    }

    const char *cmd_str = cmd->valuestring;

    if (strcmp(cmd_str, "SCRAM") == 0) {
        agent_send_scram();
    } else if (strcmp(cmd_str, "RESET_NORMAL") == 0) {
        agent_send_reset_normal();
    } else if (strcmp(cmd_str, "SET_POWER") == 0) {
        cJSON *val = cJSON_GetObjectItem(root, "value");
        int32_t power = cJSON_IsNumber(val) ? (int32_t)val->valueint : 50;
        agent_send_set_power(power);
    } else {
        ESP_LOGW(TAG, "Unknown command: %s", cmd_str);
    }

    cJSON_Delete(root);
}

static void handle_telemetry(const uint8_t *payload, uint8_t len)
{
    if (len != TELEMETRY_LEN) {
        ESP_LOGW(TAG, "telemetry payload wrong length: %u (expected %u)",
                 (unsigned)len, (unsigned)TELEMETRY_LEN);
        return;
    }

    uint32_t sample_id = 0;
    float temp_c = 0.0f;
    float accel_mag = 0.0f;
    uint8_t state = 0;
    uint8_t power = 0;

    memcpy(&sample_id, payload + 0, sizeof(sample_id));
    memcpy(&temp_c,    payload + 4, sizeof(temp_c));
    memcpy(&accel_mag, payload + 8, sizeof(accel_mag));
    state = payload[12];
    power = payload[13];

    reactor_telemetry_t t = {
        .sample_id = sample_id,
        .temp_c = temp_c,
        .accel_mag = accel_mag,
        .state = state,
        .power = power,
    };
    (void)telemetry_sender_update(&t);

    // Plain human-readable logging (no custom protocol for logging)
    ESP_LOGI(TAG, "telemetry: sample=%" PRIu32 " temp=%.1fC accel=%.2fg state=%s(%u) power=%u%%",
             sample_id, temp_c, accel_mag, state_name(state), (unsigned)state, (unsigned)power);

    // If you ever want additional logic, e.g. SCRAM on extreme temp, you'd do it here:
    // if (temp_c > 95.0f) agent_send_scram();
}

static void on_valid_frame(uint8_t msg_type, const uint8_t *payload, uint8_t len)
{
    if (msg_type == MSG_TYPE_TELEMETRY) {
        handle_telemetry(payload, len);
    } else {
        ESP_LOGW(TAG, "unhandled msg_type=0x%02X len=%u",
                 (unsigned)msg_type, (unsigned)len);
    }
}

static void fp_feed(frame_parser_t *p, const uint8_t *data, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        uint8_t b = data[i];

        switch (p->state) {
            case FP_WAIT_START:
                if (b == FRAME_START_BYTE) p->state = FP_READ_TYPE;
                break;

            case FP_READ_TYPE:
                p->msg_type = b;
                p->checksum = b;
                p->state = FP_READ_LEN;
                break;

            case FP_READ_LEN:
                p->len = b;
                p->checksum ^= b;

                if (p->len > MAX_PAYLOAD_LEN) {
                    ESP_LOGW(TAG, "invalid length %u (> %u), resync",
                             (unsigned)p->len, (unsigned)MAX_PAYLOAD_LEN);
                    fp_reset(p);
                } else if (p->len == 0) {
                    p->idx = 0;
                    p->state = FP_READ_CHECKSUM;
                } else {
                    p->idx = 0;
                    p->state = FP_READ_PAYLOAD;
                }
                break;

            case FP_READ_PAYLOAD:
                p->payload[p->idx++] = b;
                p->checksum ^= b;
                if (p->idx >= p->len) p->state = FP_READ_CHECKSUM;
                break;

            case FP_READ_CHECKSUM:
                if (p->checksum == b) {
                    on_valid_frame(p->msg_type, p->payload, p->len);
                } else {
                    ESP_LOGW(TAG, "checksum fail (type=0x%02X len=%u) expected=0x%02X got=0x%02X",
                             (unsigned)p->msg_type, (unsigned)p->len,
                             (unsigned)p->checksum, (unsigned)b);
                }
                fp_reset(p);
                break;

            default:
                fp_reset(p);
                break;
        }
    }
}

static void init_uart_link(void)
{
    uart_config_t cfg = {
        .baud_rate = BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
#if defined(UART_SCLK_APB)
        .source_clk = UART_SCLK_APB,
#endif
    };

    ESP_ERROR_CHECK(uart_param_config(UART_LINK, &cfg));

    // TX/RX pins for UART2
    ESP_ERROR_CHECK(uart_set_pin(UART_LINK,
                                 LINK_TX_PIN,  // agent TX -> reactor RX (wire this for commands)
                                 LINK_RX_PIN,  // agent RX <- reactor TX
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));

    ESP_ERROR_CHECK(uart_driver_install(UART_LINK, RX_BUF_BYTES, 0, 0, NULL, 0));
    uart_flush_input(UART_LINK);

    ESP_LOGI(TAG, "UART2 link ready: baud=%d, agent RX=GPIO%d, TX=GPIO%d",
             BAUD_RATE, (int)LINK_RX_PIN, (int)LINK_TX_PIN);
}

static void reactor_rx_task(void *arg)
{
    (void)arg;
    frame_parser_t parser;
    fp_reset(&parser);

    uint8_t buf[128];

    for (;;) {
        int n = uart_read_bytes(UART_LINK, buf, sizeof(buf), pdMS_TO_TICKS(50));
        if (n > 0) fp_feed(&parser, buf, (size_t)n);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "agent booting");

    // Initialize WiFi and connect
    esp_err_t wifi_ret = wifi_init_sta();
    if (wifi_ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected, starting MQTT");

        // Register command handler before starting MQTT
        set_mqtt_cmd_callback(handle_mqtt_command);

        // MQTT Configuration
        telemetry_config_t telem_config = {
            .broker_uri = "mqtt://alderaan.software-engineering.ie:1883",
            .client_id = "reactor_bridge_agent",
            .pub_topic = "reactor/sensors",
            .cmd_topic = "reactor/commands",  // Subscribe for commands
            .interval_ms = 1000,  // Send every 1 second
            .count = 0,           // 0 = send forever
        };
        start_telemetry_sender(&telem_config);
    } else {
        ESP_LOGW(TAG, "WiFi connection failed, skipping MQTT");
    }

    init_uart_link();

    xTaskCreate(reactor_rx_task, "reactor_rx", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "listening for telemetry frames...");
}
