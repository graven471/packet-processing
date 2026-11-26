#include "parser.h"

#include <endian.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sz/macros.h"
#include "sz/span.h"

static inline uint32_t read_u32be(const void *p) {
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return be32toh(v);
}

static inline uint16_t read_u16be(const void *p) {
    uint16_t v;
    memcpy(&v, p, sizeof(v));
    return be16toh(v);
}

// definition:
// RTP (Real-time Transport Protocol) is a protocol used to deliver audio and video over IP
// networks.
//  It doesn't guarantee delivery or timing it just packages the data so it can be played back
//  correctly at the receiver.

rtp_parse_result rtp_parse(sz_span rtp, struct rtp_packet *_Nonnull out) {
    // [RTP header 12 bytes] [RTP payload]
    /*
       0                   1                   2                   3
       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |V=2|P|X|  CC   |M|     PT      |       sequence number         |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                           timestamp                           |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |           synchronization source (SSRC) identifier            |
      +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
      |            contributing source (CSRC) identifiers             |
      |                             ....                              |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      RTP header according to RFC 3550
     */

    if (rtp.len < RTP_HDR_LEN)
        return RTP_PARSE_TRUNCATED;

    sz_span rtp_hdr = sz_span_subspan(rtp, 0, RTP_HDR_LEN);

    if (((*sz_span_first(rtp_hdr) >> 6) & 0x03) != 2)
        return RTP_PARSE_INVALID_VERSION;

    uint8_t p = (*sz_span_first(rtp_hdr) >> 5) & 0x01;
    uint8_t x = (*sz_span_first(rtp_hdr) >> 4) & 0x01;
    out->cc = *sz_span_first(rtp_hdr) & 0x0F;

    out->marker = (*sz_span_at(rtp_hdr, 1) >> 7) & 0x01;
    out->payload_type = *sz_span_at(rtp_hdr, 1) & 0x7F;

    uint16_t raw;
    memcpy(&raw, sz_span_at(rtp_hdr, 2), sizeof(uint16_t));
    out->sequence_number = be16toh(raw);

    out->timestamp = read_u32be(sz_span_at(rtp_hdr, 4));
    out->ssrc = read_u32be(sz_span_at(rtp_hdr, 8));

    // each `cc` entries occupies 4-byte
    size_t offset = RTP_HDR_LEN + (out->cc * 4);

    if (UNLIKELY(x)) {
        if (rtp.len - offset < 4)
            return RTP_PARSE_TRUNCATED;

        // Extension length is in 32-bit words.
        uint16_t ext_len = read_u16be(rtp.ptr + offset + 2);

        size_t ext_size = 4 + (size_t)ext_len * 4;

        if (ext_size > rtp.len - offset)
            return RTP_PARSE_TRUNCATED;

        offset += ext_size;
    }

    // nal starts from RTP_HDR_LEN + csrc_size
    if (sz_span_size(rtp) < offset)
        return RTP_PARSE_TRUNCATED;

    size_t payload_len = sz_span_size(rtp) - offset;

    if (p) {
        if (payload_len == 0)
            return RTP_PARSE_INVALID_PADDING;

        const uint8_t padding_len = *sz_span_last(rtp);
        payload_len -= padding_len;
    }

    out->payload = sz_span_subspan(rtp, offset, payload_len);

    if (sz_span_dangling(out->payload))
        return RTP_PARSE_TRUNCATED; /* (todo): change error code */

    return RTP_PARSE_OK;
}
