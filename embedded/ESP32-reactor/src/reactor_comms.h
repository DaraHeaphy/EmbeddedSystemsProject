#pragma once

#include <stdint.h>
#include "freertos/queue.h"
#include "reactor_control.h"

// Frame definitions (shared with agent)
#define FRAME_START_BYTE       0xAA
#define MSG_TYPE_TELEMETRY     0x01   // ESP32 -> Agent
#define MSG_TYPE_COMMAND       0x10   // Agent  -> ESP32

// Must be called before tasks start using the UART
void reactor_comms_init_uart(void);

// Register queue to push decoded commands to
void reactor_comms_set_command_queue(QueueHandle_t command_queue);

// Encode and send one telemetry frame
void reactor_comms_send_telemetry(const reactor_telemetry_t *t);

// Feed raw bytes from UART into internal frame parser
void reactor_comms_process_rx_bytes(const uint8_t *data, uint32_t len);
