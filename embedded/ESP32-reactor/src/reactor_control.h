#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "protocol.h"

typedef enum {
    REACTOR_STATE_NORMAL = 0,
    REACTOR_STATE_WARNING,
    REACTOR_STATE_SCRAM
} reactor_state_t;

typedef enum {
    CMD_NONE = 0,
    CMD_SCRAM,
    CMD_RESET_NORMAL,
    CMD_SET_POWER,
} reactor_cmd_type_t;

typedef struct {
    reactor_cmd_type_t type;
    int32_t value;
} reactor_command_t;

typedef struct {
    uint32_t sample_id;
    float temperature_c;
    float accel_mag;
    reactor_state_t state;
    uint8_t power_percent;
} reactor_telemetry_t;

#define TEMP_WARNING   45.0f
#define TEMP_CRITICAL  50.0f

void reactor_control_init(void);
void reactor_control_handle_command(const reactor_command_t *cmd);
void reactor_control_step(uint32_t sample_id, reactor_telemetry_t *out);
reactor_state_t reactor_control_get_state(void);
uint8_t reactor_control_get_power(void);
