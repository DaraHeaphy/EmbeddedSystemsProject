#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "reactor_control.h"

#define COMMS_UART_NUM   UART_NUM_2
#define COMMS_TX_PIN     GPIO_NUM_17
#define COMMS_RX_PIN     GPIO_NUM_16
#define COMMS_BAUD       115200

void comms_init(void);
void comms_set_command_queue(QueueHandle_t queue);
void comms_send_telemetry(const reactor_telemetry_t *t);
void comms_process_rx(const uint8_t *data, uint32_t len);
