#include "h264.h"

#include <rte_memcpy.h>
#include <stdint.h>
#include <stdio.h>

#include "rtp/parser.h"
#include "sz/arena.h"
#include "sz/macros.h"
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

// (todo): learn about this
typedef enum {
    H264_NAL_UNSPECIFIED = 0,
    H264_NAL_SLICE = 1,
    H264_NAL_IDR = 5,
    H264_NAL_SEI = 6,
    H264_NAL_SPS = 7,
    H264_NAL_PPS = 8,
    H264_NAL_AUD = 9,
} h264_nal_content_type_t;

[[clang::internal_linkage, clang::always_inline]] static inline nal_unit_type_t
get_nal_type(uint8_t nal) {
    return nal_type_table[nal & 0x1F];
}

[[clang::internal_linkage]] static void handle_nal_single(sz_span rtp_payload,
                                                          h264_frame_t *out_frame) {
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

    uint8_t *ptr = sz_arena_alloc(&out_frame->arena, rtp_payload.len);
    if (!ptr)
        return;
    rte_memcpy(ptr, rtp_payload.ptr, rtp_payload.len);
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

h264_result h264_init(struct h264_context *context) {
    return SZ_H264_OK;
}

h264_result h264_get_frame(const struct rtp_packet *_Nonnull packet, h264_frame_t *frame) {
    // 96 = H264
    if (UNLIKELY((packet->payload_type != RTP_PT_H264)))
        return SZ_H264_INTERNAL_ERROR;

    sz_span payload = packet->payload;

    printf("seq=%u timestamp=%u marker=%u nal_type=%u\n",
           packet->sequence_number,
           packet->timestamp,
           packet->marker,
           packet->payload.ptr[0] & 0x1F);

    const uint8_t nal = *sz_span_first(payload);
    // this is forbidden bit must be 0
    if (UNLIKELY(((nal >> 7) & 0x01) != 0))
        return SZ_H264_INTERNAL_ERROR;

    const uint8_t nri = (nal >> 5) & 0x3;
    const nal_unit_type_t nal_type = get_nal_type(nal);

    struct sz_arena_config arena_cfg = {.capacity = 2097152,
                                        .pre_fault = true,
                                        .flags = ARENA_FLAG_HUGE_PAGES};

    // (fixme): recreating arena each time lol
    if (sz_arena_init(&arena_cfg, &frame->arena) != 0)
        return SZ_H264_INTERNAL_ERROR;

    switch (nal_type) {
    case NAL_TYPE_SINGLE:
        handle_nal_single(payload, frame);
        if (packet->marker)
            break;
        break;

    // Aggregation Packets
    case NAL_TYPE_STAP_A:
        printf("nal stap-a\n");
        // handle_nal_stap_a(payload);
        return SZ_H264_NOT_IMPLEMENTED;

    case NAL_TYPE_FU_A:
        printf("nal fu-a\n");
        return SZ_H264_NOT_IMPLEMENTED;

    case NAL_TYPE_UNKNOWN:
    case NAL_UNDEFINED:
        return SZ_H264_INTERNAL_ERROR;
    }

    return SZ_H264_OK;
}
