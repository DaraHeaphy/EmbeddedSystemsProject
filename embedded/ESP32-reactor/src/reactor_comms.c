#include "reactor_comms.h"

#include <string.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "protocol.h"
#include "frame_parser.h"

static const char *TAG = "comms";

static QueueHandle_t s_cmd_queue = NULL;
static frame_parser_t s_parser;

void comms_set_command_queue(QueueHandle_t queue)
{
    s_cmd_queue = queue;
}

// sends a framed message over uart
static void send_frame(uint8_t msg_type, const uint8_t *payload, uint8_t len)
{
    uint8_t header[3] = { FRAME_START_BYTE, msg_type, len };
    uint8_t checksum = protocol_calc_checksum(msg_type, len, payload);

    uart_write_bytes(COMMS_UART_NUM, (const char *)header, 3);
    if (len > 0 && payload) {
        uart_write_bytes(COMMS_UART_NUM, (const char *)payload, len);
    }
    uart_write_bytes(COMMS_UART_NUM, (const char *)&checksum, 1);
}

void comms_send_telemetry(const reactor_telemetry_t *t)
{
    if (!t) return;

    uint8_t payload[TELEMETRY_PAYLOAD_LEN];
    uint8_t *p = payload;

    memcpy(p, &t->sample_id, sizeof(uint32_t));
    p += sizeof(uint32_t);
    memcpy(p, &t->temperature_c, sizeof(float));
    p += sizeof(float);
    memcpy(p, &t->accel_mag, sizeof(float));
    p += sizeof(float);
    *p++ = (uint8_t)t->state;
    *p++ = t->power_percent;

    send_frame(MSG_TYPE_TELEMETRY, payload, TELEMETRY_PAYLOAD_LEN);
}

// callback when a valid frame is received
static void on_frame(void *ctx, uint8_t msg_type, const uint8_t *payload, uint8_t len)
{
    (void)ctx;

    if (msg_type != MSG_TYPE_COMMAND) {
        ESP_LOGW(TAG, "unexpected msg_type=0x%02X", msg_type);
        return;
    }

    if (!s_cmd_queue || len < 1) {
        return;
    }

    uint8_t cmd_id = payload[0];
    reactor_command_t cmd = {0};

    switch (cmd_id) {
    case CMD_ID_SCRAM:
        cmd.type = CMD_SCRAM;
        break;

    case CMD_ID_RESET_NORMAL:
        cmd.type = CMD_RESET_NORMAL;
        break;

    case CMD_ID_SET_POWER:
        if (len < 5) {
            ESP_LOGW(TAG, "SET_POWER frame too short");
            return;
        }
        memcpy(&cmd.value, &payload[1], sizeof(int32_t));
        cmd.type = CMD_SET_POWER;
        break;

    default:
        ESP_LOGW(TAG, "unknown cmd_id=%u", cmd_id);
        return;
    }

    if (xQueueSend(s_cmd_queue, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "cmd queue full");
    }
}

void comms_init(void)
{
    uart_config_t cfg = {
        .baud_rate = COMMS_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
#ifdef UART_SCLK_APB
        .source_clk = UART_SCLK_APB,
#endif
    };

    ESP_ERROR_CHECK(uart_param_config(COMMS_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(COMMS_UART_NUM, COMMS_TX_PIN, COMMS_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(COMMS_UART_NUM, 256, 0, 0, NULL, 0));

    frame_parser_init(&s_parser, on_frame, NULL);

    ESP_LOGI(TAG, "uart ready: baud=%d tx=GPIO%d rx=GPIO%d",
             COMMS_BAUD, COMMS_TX_PIN, COMMS_RX_PIN);
}

void comms_process_rx(const uint8_t *data, uint32_t len)
{
    if (data && len > 0) {
        frame_parser_feed(&s_parser, data, len);
    }
}
