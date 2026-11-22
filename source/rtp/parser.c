#include "parser.h"

#include <endian.h>
#include <stdint.h>
#include <stdio.h>

#include "sz/span.h"

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
    uint8_t cc = *sz_span_first(rtp_hdr) & 0x0F;

    uint8_t m = (*sz_span_at(rtp_hdr, 1) >> 7) & 0x01;
    uint8_t pt = *sz_span_at(rtp_hdr, 1) & 0x7F;

    uint16_t seq_num = be16toh(*(uint16_t *)(sz_span_at(rtp_hdr, 2)));
    uint32_t timestamp = be32toh(*(uint32_t *)(sz_span_at(rtp_hdr, 4)));
    uint32_t ssrc = be32toh(*(uint32_t *)(sz_span_at(rtp_hdr, 8)));

    // each `cc` entries occupies 4-byte
    size_t offset = RTP_HDR_LEN + (cc * 4);

    if (x) [[clang::unlikely]] {
        uint16_t ext_len =
            ((uint16_t)rtp_hdr.ptr[offset + 2] << 8) | ((uint16_t)rtp_hdr.ptr[offset + 3]);
        offset += 4 + (ext_len * 4);
    }

    // nal starts from RTP_HDR_LEN + csrc_size
    if (sz_span_size(rtp) < offset)
        return RTP_PARSE_TRUNCATED;

    size_t payload_len = sz_span_size(rtp) - offset;

    if (p) {
        const uint8_t padding_len = *sz_span_last(rtp);
        payload_len -= padding_len;
    }

    sz_span rtp_payload = sz_span_subspan(rtp, offset, payload_len);

    out->cc = cc;
    out->ssrc = ssrc;
    out->marker = m;
    out->payload_type = pt;
    out->sequence_number = seq_num;
    out->timestamp = timestamp;
    out->payload = rtp_payload;

    return RTP_PARSE_OK;
}
