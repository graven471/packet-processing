#include "nal.h"

#include "sz/span.h"

typedef enum {
    NAL_TYPE_UNKNOWN = 0,
    NAL_TYPE_SINGLE,
    NAL_TYPE_STAP_A,
    NAL_TYPE_FU_A,
    NAL_UNDEFINED
} nal_unit_type_t;

static const nal_unit_type_t nal_type_table[32] = {
    [1 ... 23] = NAL_TYPE_SINGLE,
    [24] = NAL_TYPE_STAP_A,
    [25 ... 27] = NAL_TYPE_UNKNOWN, // we don't support these
    [28] = NAL_TYPE_FU_A,
    [29] = NAL_UNDEFINED,
    [30 ... 31] = NAL_UNDEFINED,
};

[[clang::internal_linkage, clang::always_inline]] static inline nal_unit_type_t
get_nal_type(uint8_t nal) {
    return nal_type_table[nal & 0x1F];
}

[[clang::internal_linkage]] static void handle_nal_single(sz_span rtp_payload) {
    /*
     0                   1                   2                   3
     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |F|NRI|  Type   |                                               |
    +-+-+-+-+-+-+-+-+                                               |
    |                                                               |
    |               Bytes 2..n of a single NAL unit                 |
    |                                                               |
    |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                               :...OPTIONAL RTP padding        |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    Figure 2.  RTP payload format for single NAL unit packet
    RFC 6184 section 5.6
   */
}

[[clang::internal_linkage]] static void handle_nal_fu_a(sz_span rtp_payload) {
    /*
     this payload type allows fragmenting a NAL unit into several RTP packets.
     Fragmentation is defined only for a single NAL unit and not for any
     aggregation packets
     Fragments of the same NAL unit MUST be sent in consecutive order with
     ascending RTP sequence numbers (with no other RTP packets within the
     same RTP packet stream being sent between the first and last fragment).

     NAL unit MUST be reassembled in RTP sequence number order.

     0                   1                   2                   3
     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    | FU indicator  |   FU header   |                               |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               |
    |                                                               |
    |                         FU payload                            |
    |                                                               |
    |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                               :...OPTIONAL RTP padding        |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

    Figure 14.  RTP payload format for FU-A
    FU indicator layout is same as regualar NAL header

    FU header is defined like this:
    +---------------+
    |S|E|R|  Type   |
    +---------------+

    S: 1 bit
     when set to one, the start bit indicates the start of a fragmented NAL unit basically the first
     packet of FU-A will have S = 1

    E: 1 bit
      when set to one, the End bit indicates the end of a fragmented NAL unit i.e., the last byte of
      the payload is also the last byte of the fragmented NAL unit.

    R: 1 bit
      Reserved bit MUST be equal to 0 and MUST be ignored by the receiver.

    Type 5 bits
      The NAL unit payload type
    */

    const uint8_t *fu_header = sz_span_at(rtp_payload, 1);
    if (fu_header == nullptr)
        return;

    const uint8_t pt = *fu_header & 0x1F;
    const uint8_t start = (*fu_header >> 7) & 0x01;
    const uint8_t end = (*fu_header >> 6) & 0x01;

    // start = 1, end = 0 -> start of fragmented packet
    // start = 0, end = 0 -> middle of fragmented packet
    // start = 0, end = 1 -> end of fragment packet
}

[[clang::internal_linkage]] static void handle_nal_stap_a(sz_span rtp_payload) {}

// (todo): let this just do parsing and return header and other useful utilities then
// another layer will do remaining work like identifying nal running logic for
// different nal etc
void h264_nal_parse(struct rtp_packet *_Nonnull packet) {
    // 96 = H264
    if (packet->payload_type != RTP_PT_H264) [[clang::unlikely]]
        return;

    sz_span payload = packet->payload;

    const uint8_t nal = *sz_span_first(payload);
    // this is forbidden bit must be 0
    if (((nal >> 7) & 0x01) != 0)
        return;

    // nal_ref_idc tells how important nal is for reference
    // the NRI value of non-reference pictures is 00 as mandated by H.264.
    // const uint8_t nri = (nal >> 5) & 0x3;

    // (rfc): The single NAL unit mode is targeted for conversational systems that comply with ITU-T
    // Recommendation H.241 so nal_unit_type from 1-23 is "single NAL unit mode" 24 — STAP-A 28 —
    // FU-A
    const nal_unit_type_t nal_type = get_nal_type(nal);

    switch (nal_type) {
    case NAL_TYPE_SINGLE:
        handle_nal_single(payload);
        break;

    // Aggregation Packets
    case NAL_TYPE_STAP_A:
        handle_nal_stap_a(payload);
        break;

    case NAL_TYPE_FU_A:
        handle_nal_fu_a(payload);
        break;

    case NAL_TYPE_UNKNOWN:
    case NAL_UNDEFINED:
        break;
    }
}
