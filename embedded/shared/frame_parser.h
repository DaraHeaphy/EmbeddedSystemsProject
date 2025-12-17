#pragma once

#include <stdint.h>
#include <stddef.h>
#include "protocol.h"

typedef void (*frame_callback_t)(void *ctx, uint8_t msg_type, const uint8_t *payload, uint8_t len);

typedef enum {
    FP_WAIT_START,
    FP_READ_TYPE,
    FP_READ_LEN,
    FP_READ_PAYLOAD,
    FP_READ_CHECKSUM
} frame_parser_state_t;

typedef struct {
    frame_parser_state_t state;
    uint8_t msg_type;
    uint8_t len;
    uint8_t idx;
    uint8_t checksum;
    uint8_t payload[MAX_PAYLOAD_LEN];
    frame_callback_t callback;
    void *callback_ctx;
} frame_parser_t;

void frame_parser_init(frame_parser_t *parser, frame_callback_t callback, void *ctx);
void frame_parser_feed(frame_parser_t *parser, const uint8_t *data, size_t len);
void frame_parser_reset(frame_parser_t *parser);
