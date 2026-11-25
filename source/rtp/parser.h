#pragma once

#include <stddef.h>
#include <stdint.h>

#include "rtp/serial_number.h"
#include "sz/span.h"

#define RTP_HDR_LEN 12

typedef enum {
    RTP_PARSE_OK = 0,
    RTP_PARSE_INVALID_ARGUMENT,
    RTP_PARSE_TRUNCATED,
    RTP_PARSE_INVALID_VERSION,
    RTP_PARSE_INVALID_HEADER,
    RTP_PARSE_INVALID_EXTENSION,
    RTP_PARSE_INVALID_PADDING,
} rtp_parse_result;

struct rtp_packet {
    uint8_t cc;
    bool marker;
    uint8_t payload_type;
    seq_num sequence_number;
    uint32_t timestamp;
    uint32_t ssrc;
    sz_span payload;
};

rtp_parse_result rtp_parse(sz_span rtp, struct rtp_packet *_Nonnull out);
