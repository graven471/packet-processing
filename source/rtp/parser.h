#pragma once

#include <stddef.h>
#include <stdint.h>

#include "rtp/serial_number.h"
#include "sz/span.h"
#include "sz/sz_types.h"

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
    u8 cc;
    bool marker;
    u8 payload_type;
    seq_num sequence_number;
    u32 timestamp;
    u32 ssrc;
    sz_span payload;
};

rtp_parse_result rtp_parse(sz_span rtp, struct rtp_packet *_Nonnull out);
