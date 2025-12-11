// reactor_comms.c
#include "reactor_comms.h"

#include <string.h>
#include <stdarg.h>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "reactor_comms";

#define REACTOR_UART_NUM           UART_NUM_0
#define REACTOR_UART_TX_PIN        GPIO_NUM_1
#define REACTOR_UART_RX_PIN        GPIO_NUM_3
#define REACTOR_UART_BAUD_RATE     115200
#define REACTOR_UART_RX_BUF_SIZE   256
#define REACTOR_UART_TX_BUF_SIZE   0     // use FIFO only, blocking write

static QueueHandle_t s_command_queue = NULL;

// --- UART setup ---------------------------------------------------------

void reactor_comms_init_uart(void)
{
    uart_config_t uart_config = {
        .baud_rate = REACTOR_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    #ifdef UART_SCLK_APB
        .source_clk = UART_SCLK_APB,
    #endif
    };

    ESP_ERROR_CHECK(uart_param_config(REACTOR_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(REACTOR_UART_NUM,
                                 REACTOR_UART_TX_PIN,
                                 REACTOR_UART_RX_PIN,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));

    ESP_ERROR_CHECK(uart_driver_install(REACTOR_UART_NUM,
                                        REACTOR_UART_RX_BUF_SIZE,
                                        REACTOR_UART_TX_BUF_SIZE,
                                        0, NULL, 0));

    ESP_LOGI(TAG, "UART initialised on UART%d @ %d baud",
             REACTOR_UART_NUM, REACTOR_UART_BAUD_RATE);
}

void reactor_comms_set_command_queue(QueueHandle_t command_queue)
{
    s_command_queue = command_queue;
}

// --- Frame helpers ------------------------------------------------------

static uint8_t calc_checksum(uint8_t msg_type, uint8_t length,
                             const uint8_t *payload)
{
    uint8_t c = msg_type ^ length;
    for (uint8_t i = 0; i < length; ++i) {
        c ^= payload[i];
    }
    return c;
}

static void uart_send_frame(uint8_t msg_type,
                            const uint8_t *payload,
                            uint8_t length)
{
    uint8_t header[3] = {
        FRAME_START_BYTE,
        msg_type,
        length
    };
    uint8_t checksum = calc_checksum(msg_type, length, payload);

    uart_write_bytes(REACTOR_UART_NUM, (const char *)header, 3);
    if (length > 0 && payload != NULL) {
        uart_write_bytes(REACTOR_UART_NUM, (const char *)payload, length);
    }
    uart_write_bytes(REACTOR_UART_NUM, (const char *)&checksum, 1);
}

// Telemetry encoding: <u32 sample_id><float temp><float accel><u8 state><u8 power>
void reactor_comms_send_telemetry(const reactor_telemetry_t *t)
{
    if (!t) return;

    uint8_t payload[14];
    uint8_t *p = payload;

    memcpy(p, &t->sample_id, sizeof(uint32_t));
    p += sizeof(uint32_t);

    memcpy(p, &t->temperature_c, sizeof(float));
    p += sizeof(float);

    memcpy(p, &t->accel_mag, sizeof(float));
    p += sizeof(float);

    *p++ = (uint8_t)t->state;
    *p++ = t->power_percent;

    uint8_t len = (uint8_t)(p - payload);
    uart_send_frame(MSG_TYPE_TELEMETRY, payload, len);
}

// --- RX side: frame parser + command dispatch ---------------------------

static void comms_handle_command_frame(const uint8_t *payload,
                                       uint8_t length)
{
    if (!s_command_queue) {
        ESP_LOGW(TAG, "Command queue not set, dropping command frame");
        return;
    }

    if (length < 1) {
        ESP_LOGW(TAG, "COMMAND frame too short");
        return;
    }

    uint8_t cmd_id = payload[0];
    reactor_command_t cmd = {0};

    switch (cmd_id) {
        case CMD_SCRAM:
            cmd.type = CMD_SCRAM;
            break;

        case CMD_RESET_NORMAL:
            cmd.type = CMD_RESET_NORMAL;
            break;

        case CMD_SET_POWER:
            if (length < 1 + 4) {
                ESP_LOGW(TAG, "SET_POWER frame too short");
                return;
            } else {
                int32_t power = 0;
                memcpy(&power, &payload[1], sizeof(int32_t));
                cmd.type  = CMD_SET_POWER;
                cmd.value = power;
            }
            break;

        default:
            ESP_LOGW(TAG, "Unknown command id: %u", (unsigned)cmd_id);
            return;
    }

    if (xQueueSend(s_command_queue, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Command queue full, dropping command");
    }
}

static void comms_process_rx_byte(uint8_t byte)
{
    typedef enum {
        RX_WAIT_START = 0,
        RX_READ_TYPE,
        RX_READ_LENGTH,
        RX_READ_PAYLOAD,
        RX_READ_CHECKSUM
    } rx_state_t;

    static rx_state_t state   = RX_WAIT_START;
    static uint8_t    msg_type = 0;
    static uint8_t    length   = 0;
    static uint8_t    payload[64];
    static uint8_t    index    = 0;
    static uint8_t    checksum = 0;

    switch (state) {
        case RX_WAIT_START:
            if (byte == FRAME_START_BYTE) {
                state = RX_READ_TYPE;
            }
            break;

        case RX_READ_TYPE:
            msg_type = byte;
            checksum = msg_type;
            state    = RX_READ_LENGTH;
            break;

        case RX_READ_LENGTH:
            length   = byte;
            checksum ^= length;

            if (length > sizeof(payload)) {
                ESP_LOGW(TAG, "Frame length too large: %u", (unsigned)length);
                state = RX_WAIT_START;
            } else if (length == 0) {
                index = 0;
                state = RX_READ_CHECKSUM;
            } else {
                index = 0;
                state = RX_READ_PAYLOAD;
            }
            break;

        case RX_READ_PAYLOAD:
            payload[index++] = byte;
            checksum ^= byte;
            if (index >= length) {
                state = RX_READ_CHECKSUM;
            }
            break;

        case RX_READ_CHECKSUM:
            if (checksum == byte) {
                if (msg_type == MSG_TYPE_COMMAND) {
                    comms_handle_command_frame(payload, length);
                } else {
                    ESP_LOGW(TAG, "Unhandled msg_type=0x%02X len=%u",
                             (unsigned)msg_type, (unsigned)length);
                }
            } else {
                ESP_LOGW(TAG,
                         "Checksum error (expected 0x%02X got 0x%02X)",
                         (unsigned)checksum, (unsigned)byte);
            }
            state = RX_WAIT_START;
            break;

        default:
            state = RX_WAIT_START;
            break;
    }
}

void reactor_comms_process_rx_bytes(const uint8_t *data, uint32_t len)
{
    if (!data) return;
    for (uint32_t i = 0; i < len; ++i) {
        comms_process_rx_byte(data[i]);
    }
}
