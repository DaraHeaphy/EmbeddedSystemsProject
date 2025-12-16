#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "protocol.h"

typedef void (*frame_cb_t)(void *ctx, uint8_t msg_type, const uint8_t *payload, uint8_t len);

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
    frame_cb_t cb;
    void *cb_ctx;
} frame_parser_t;

void frame_parser_init(frame_parser_t *p, frame_cb_t cb, void *cb_ctx);
void frame_parser_feed(frame_parser_t *p, const uint8_t *data, size_t n);
