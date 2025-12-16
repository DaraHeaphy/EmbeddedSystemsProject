#include "frame_parser.h"

void frame_parser_init(frame_parser_t *p, frame_cb_t cb, void *cb_ctx)
{
    p->state = FP_WAIT_START;
    p->msg_type = 0;
    p->len = 0;
    p->idx = 0;
    p->checksum = 0;
    p->cb = cb;
    p->cb_ctx = cb_ctx;
}

static inline void fp_reset(frame_parser_t *p)
{
    p->state = FP_WAIT_START;
    p->idx = 0;
    p->len = 0;
    p->checksum = 0;
}

void frame_parser_feed(frame_parser_t *p, const uint8_t *data, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        uint8_t b = data[i];

        switch (p->state) {
            case FP_WAIT_START:
                if (b == FRAME_START_BYTE) p->state = FP_READ_TYPE;
                break;

            case FP_READ_TYPE:
                p->msg_type = b;
                p->checksum = b;
                p->state = FP_READ_LEN;
                break;

            case FP_READ_LEN:
                p->len = b;
                p->checksum ^= b;

                if (p->len == 0) {
                    p->idx = 0;
                    p->state = FP_READ_CHECKSUM;
                } else if (p->len > MAX_PAYLOAD_LEN) {
                    fp_reset(p);
                } else {
                    p->idx = 0;
                    p->state = FP_READ_PAYLOAD;
                }
                break;

            case FP_READ_PAYLOAD:
                p->payload[p->idx++] = b;
                p->checksum ^= b;
                if (p->idx >= p->len) p->state = FP_READ_CHECKSUM;
                break;

            case FP_READ_CHECKSUM:
                if (p->checksum == b) {
                    if (p->cb) p->cb(p->cb_ctx, p->msg_type, p->payload, p->len);
                }
                fp_reset(p);
                break;

            default:
                fp_reset(p);
                break;
        }
    }
}
