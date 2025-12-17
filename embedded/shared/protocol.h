#pragma once

#include <stdint.h>

// frame format: [START] [TYPE] [LEN] [PAYLOAD...] [CHECKSUM]
#define FRAME_START_BYTE    0xAA
#define MAX_PAYLOAD_LEN     64

// message types
#define MSG_TYPE_TELEMETRY  0x01
#define MSG_TYPE_COMMAND    0x10

// command ids
#define CMD_ID_SCRAM        1
#define CMD_ID_RESET_NORMAL 2
#define CMD_ID_SET_POWER    3

// telemetry payload: u32 sample_id, f32 temp, f32 accel, u8 state, u8 power
#define TELEMETRY_PAYLOAD_LEN 14

static inline uint8_t protocol_calc_checksum(uint8_t msg_type, uint8_t len, const uint8_t *payload)
{
    uint8_t c = msg_type ^ len;
    for (uint8_t i = 0; i < len; i++) {
        c ^= payload[i];
    }
    return c;
}
