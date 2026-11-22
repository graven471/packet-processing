#include "nal.h"

typedef enum {
    NAL_TYPE_UNKNOWN = 0,
    NAL_TYPE_SINGLE,
    NAL_TYPE_STAP_A,
    NAL_TYPE_FU_A,
    NAL_UNDEFINED
} nal_unit_type_t;

static const nal_unit_type_t nal_type_table[32] = {
    [1 ... 23] = NAL_TYPE_SINGLE, // gcc extension
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

void h264_nal_parse(struct rtp_packet *_Nonnull packet) {
    // 96 = H264
    if (packet->payload_type != RTP_PT_H264)
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
    case NAL_TYPE_FU_A:
    case NAL_TYPE_UNKNOWN:
    case NAL_UNDEFINED:
        break;
    }
}
