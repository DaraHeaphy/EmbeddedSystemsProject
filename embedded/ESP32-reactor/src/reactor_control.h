#pragma once

#include <stdint.h>
#include <stdbool.h>

// Reactor state machine
typedef enum {
    REACTOR_STATE_NORMAL = 0,
    REACTOR_STATE_WARNING,
    REACTOR_STATE_SCRAM
} reactor_state_t;

// Command types used by comms -> control
typedef enum {
    CMD_NONE = 0,
    CMD_SCRAM,          // 1
    CMD_RESET_NORMAL,   // 2
    CMD_SET_POWER,      // 3  (value = 0..100)
} reactor_command_type_t;

typedef struct {
    reactor_command_type_t type;
    int32_t value;      // used for CMD_SET_POWER (0..100); 0 for others
} reactor_command_t;

// Telemetry struct used by control -> comms
typedef struct {
    uint32_t        sample_id;
    float           temperature_c;
    float           accel_mag;
    reactor_state_t state;
    uint8_t         power_percent;
} reactor_telemetry_t;

#define TEMP_WARNING_DEFAULT   45.0f
#define TEMP_CRITICAL_DEFAULT  50.0f

void reactor_control_init(void);

// Apply a command (e.g. SCRAM, RESET, SET_POWER)
void reactor_control_handle_command(const reactor_command_t *cmd);

// Run one control cycle: update state, LED, fill telemetry
void reactor_control_step(uint32_t sample_id,
                          reactor_telemetry_t *out_telemetry);

// Optionally expose these for debugging / UI
reactor_state_t reactor_control_get_state(void);
uint8_t reactor_control_get_power(void);
