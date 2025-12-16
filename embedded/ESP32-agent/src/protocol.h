#pragma once
#include <stdint.h>

#define FRAME_START_BYTE       0xAA

#define MSG_TYPE_TELEMETRY     0x01
#define MSG_TYPE_COMMAND       0x10

// Keep this aligned with your Python parser guard (it rejects >64).
#define MAX_PAYLOAD_LEN        64

static inline uint8_t calc_checksum(uint8_t msg_type, uint8_t length, const uint8_t *payload)
{
    uint8_t c = (uint8_t)(msg_type ^ length);
    for (uint8_t i = 0; i < length; i++) c ^= payload[i];
    return c;
}
