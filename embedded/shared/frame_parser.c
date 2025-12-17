#include "frame_parser.h"

void frame_parser_init(frame_parser_t *parser, frame_callback_t callback, void *ctx)
{
    parser->state = FP_WAIT_START;
    parser->msg_type = 0;
    parser->len = 0;
    parser->idx = 0;
    parser->checksum = 0;
    parser->callback = callback;
    parser->callback_ctx = ctx;
}

void frame_parser_reset(frame_parser_t *parser)
{
    parser->state = FP_WAIT_START;
    parser->idx = 0;
    parser->len = 0;
    parser->checksum = 0;
}

void frame_parser_feed(frame_parser_t *parser, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];

        switch (parser->state) {
        case FP_WAIT_START:
            if (b == FRAME_START_BYTE) {
                parser->state = FP_READ_TYPE;
            }
            break;

        case FP_READ_TYPE:
            parser->msg_type = b;
            parser->checksum = b;
            parser->state = FP_READ_LEN;
            break;

        case FP_READ_LEN:
            parser->len = b;
            parser->checksum ^= b;
            if (b > MAX_PAYLOAD_LEN) {
                frame_parser_reset(parser);
            } else if (b == 0) {
                parser->state = FP_READ_CHECKSUM;
            } else {
                parser->idx = 0;
                parser->state = FP_READ_PAYLOAD;
            }
            break;

        case FP_READ_PAYLOAD:
            parser->payload[parser->idx++] = b;
            parser->checksum ^= b;
            if (parser->idx >= parser->len) {
                parser->state = FP_READ_CHECKSUM;
            }
            break;

        case FP_READ_CHECKSUM:
            if (parser->checksum == b && parser->callback) {
                parser->callback(parser->callback_ctx, parser->msg_type,
                                 parser->payload, parser->len);
            }
            frame_parser_reset(parser);
            break;

        default:
            frame_parser_reset(parser);
            break;
        }
    }
}
