#include "h264.h"

#include <assert.h>
#include <endian.h>
#include <rte_memcpy.h>
#include <stdbit.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <sys/types.h>

#include "rtp/parser.h"
#include "sz/arena.h"
#include "sz/macros.h"
#include "sz/span.h"
#include "sz/sz_types.h"

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
get_nal_type(u8 nal) {
    return nal_type_table[nal & 0x1F];
}

[[clang::internal_linkage]] static void handle_nal_single(sz_span rtp_payload,
                                                          h264_frame_t *frame) {
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

    u8 *ptr = sz_arena_alloc(&frame->arena, rtp_payload.len);
    if (!ptr) // todo: handle error
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

    const u8 *fu_header = sz_span_at(rtp_payload, 1);
    if (fu_header == nullptr)
        return;

    // const u8 pt = *fu_header & 0x1F;
    // const u8 start = (*fu_header >> 7) & 0x01;
    // const u8 end = (*fu_header >> 6) & 0x01;

    // start = 1, end = 0 -> start of fragmented packet
    // start = 0, end = 0 -> middle of fragmented packet
    // start = 0, end = 1 -> end of fragment packet
}

h264_result h264_prepare_for_frame(usize payload_capacity, h264_frame_t *frame) {
    struct sz_arena_config cfg = {.capacity = payload_capacity,
                                  .flags = ARENA_FLAG_HUGE_PAGES,
                                  .pre_fault = true};
    if (sz_arena_init(&cfg, &frame->arena) != 0)
        return SZ_H264_MEMORY_ERROR;
    return SZ_H264_OK;
}

typedef struct {
    u32 value;
    u32 bits_read;
} ue_result;

// static inline ue_result read_ue(const u8 *data, u32 bit_pos) {
//     u32 byte_pos = bit_pos / 8;
//     u32 offset = bit_pos % 8;
//
//     u32 word;
//     __builtin_memcpy(&word, data + byte_pos, sizeof(word));
//
//     word = __builtin_bswap64(word);
//     word <<= offset;
//
//     const unsigned z = stdc_leading_zeros(word);
//
//     const u32 suffix = (word >> (31 - 2 * z)) & ((1u << z) - 1);
//
//     return (ue_result){.bits_read = 2 * z + 1, .value = ((1u << z) - 1) + suffix};
// }
//

typedef struct {
    const u8 *data;
    u64 size_bits;
    u64 pos; // current bit position
    bool err;
} bitreader;

static __always_inline void br_init(bitreader *br, const u8 *data, usize size_bytes) {
    br->data = data;
    br->size_bits = (u64)size_bytes * 8;
    br->pos = 0;
    br->err = false;
}

static __always_inline unsigned get_bit(const u8 *data, u64 pos) {
    return (data[pos >> 3] >> (7 - (pos & 7))) & 1u;
}

static bool read_ue_slow(bitreader *br, u32 *out) {
    u64 pos = br->pos;
    unsigned z = 0;

    for (;;) {
        if (pos >= br->size_bits)
            return false;
        if (get_bit(br->data, pos++))
            break;
        if (++z > 31)
            return false;
    }
    if (pos + z > br->size_bits)
        return false;

    u64 suffix = 0;
    for (unsigned i = 0; i < z; i++)
        suffix = (suffix << 1) | get_bit(br->data, pos++);

    *out = (u32)(((1ull << z) - 1) + suffix);
    br->pos = pos;
    return true;
}

static __always_inline bool read_ue(bitreader *br, u32 *out) {
    // Exp-Golomb
    // [ leading zeros ][ 1 ][ information bits ]
    const u64 pos = br->pos;
    if (pos >= br->size_bits)
        return false;

    u64 word;
    __builtin_memcpy(&word, br->data + (pos >> 3), sizeof word);
    word = __builtin_bswap64(word) << (pos & 7); // >= 57 valid bits

    if (UNLIKELY(word != 0)) { // clz(0) is UB
        const unsigned z = (unsigned)__builtin_clzll(word);
        if (z <= 28) { // 2z+1 <= 57
            const unsigned len = 2 * z + 1;
            if (pos + len > br->size_bits)
                return false;

            // The 2z+1 bits read as one integer are (1<<z) | suffix,
            // and ue value = (1<<z) - 1 + suffix = that integer - 1.
            const u64 code = word >> (64 - len);
            *out = (u32)(code - 1);
            br->pos = pos + len;
            return true;
        }
    }
    return read_ue_slow(br, out);
}

static inline u32 read_ue_s(bitreader *br) {
    u32 v = 0;
    if (br->err || !read_ue(br, &v)) {
        br->err = true;
        return 0;
    }
    return v;
}

// se(v): 0, 1, -1, 2, -2, ... mapped from ue
static __always_inline bool read_se(bitreader *br, i32 *out) {
    u32 k;
    if (!read_ue(br, &k))
        return false;
    *out = (k & 1) ? (i32)((k >> 1) + 1) : -(i32)(k >> 1);
    return true;
}

static inline u32 read_se_s(bitreader *br) {
    i32 v = 0;
    if (br->err || !read_se(br, &v)) {
        br->err = true;
        return 0;
    }
    return v;
}

static __always_inline u32 read_u(bitreader *br, unsigned n) {
    if (br->err || br->pos + n > br->size_bits) {
        br->err = true;
        return 0;
    }
    u64 word;
    memcpy(&word, br->data + (br->pos >> 3), sizeof word);
    word = be64toh(word) << (br->pos & 7);
    br->pos += n;
    return (u32)(word >> (64 - n));
}

static __always_inline void skip_bits(bitreader *br, uint64_t n) {
    if (br->err || br->pos + n > br->size_bits) {
        br->err = true;
        return;
    }
    br->pos += n;
}

static __always_inline bool scaling_list(bitreader *br, u8 *scaling_list,
                                         unsigned size_of_scaling_list) {
    unsigned last_scale = 8;
    unsigned next_scale = 8;
    bool use_default_scaling_matrix_flag = false;

    for (unsigned j = 0; j < size_of_scaling_list; ++j) {
        if (next_scale != 0) {
            u8 delta_scale = read_u(br, 1);
            next_scale = (last_scale + delta_scale + 256) % 256;
            use_default_scaling_matrix_flag = (j == 0 && next_scale == 0);
        }
        scaling_list[j] = (next_scale == 0) ? last_scale : next_scale;
        last_scale = scaling_list[j];
    }
    return use_default_scaling_matrix_flag;
}

static inline struct h264_hrd_parameters h264_hrd_parameters(bitreader *br) {
    struct h264_hrd_parameters out = {0};
    out.cpb_cnt_minus1 = read_ue_s(br);
    assert(out.cpb_cnt_minus1 < 31);

    skip_bits(br, 8); // // bit_rate_scale u(4) + cpb_size_scale u(4)

    for (u32 sched_sel_idx = 0; sched_sel_idx <= out.cpb_cnt_minus1; sched_sel_idx++) {
        out.bit_rate_value_minus1[sched_sel_idx] = read_ue_s(br);
        out.cpb_size_value_minus1[sched_sel_idx] = read_ue_s(br);
        out.cbr_flag[sched_sel_idx] = read_u(br, 1);
    }

    out.initial_cpb_removal_delay_length_minus1 = read_u(br, 5);
    out.cpb_removal_delay_length_minus1 = read_u(br, 5);
    out.dpb_output_delay_length_minus1 = read_u(br, 5);
    out.time_offset_length = read_u(br, 5);

    return out;
}

static inline struct h264_vui h264_vui_parameters(bitreader *br) {
    struct h264_vui vui = {0};

    if (read_u(br, 1)) {
#define Extended_SAR 255
        if (read_u(br, 1) == Extended_SAR) {
            (void)read_u(br, 16);
            (void)read_u(br, 16);
        }
    }

    if (read_u(br, 1))
        skip_bits(br, 1);

    if (read_u(br, 1)) {
        skip_bits(br, 3); // video format

        vui.video_full_range_flag = read_u(br, 1);
        if (read_u(br, 1)) { // colour_description_present_flag
            vui.colour_primaries = read_u(br, 1);
            vui.transfer_characteristics = read_u(br, 1);
            vui.matrix_coefficients = read_u(br, 1);
        }
    }

    if (read_u(br, 1)) { // chroma_loc_info_present_flag
        // skip chroma_sample_loc_type_top_field and chroma_sample_loc_type_bottom_field
        (void)read_ue_s(br);
        (void)read_ue_s(br);
    }

    if (read_u(br, 1)) { // timing_info_present_flag
        (void)read_u(br, 32);
        (void)read_u(br, 32);
        (void)read_u(br, 1);
    }

    u8 nal_hrd_parameters_present_flag = read_u(br, 1);
    u8 vcl_hrd_parameters_present_flag = read_u(br, 1);

    if (nal_hrd_parameters_present_flag)
        (void)h264_hrd_parameters(br);

    if (vcl_hrd_parameters_present_flag)
        (void)h264_hrd_parameters(br);

    if (nal_hrd_parameters_present_flag || vcl_hrd_parameters_present_flag)
        skip_bits(br, 1); // low_delay_hrd_flag

    skip_bits(br, 1); // pic_struct_present_flag

    if (read_u(br, 1)) {  // bitstream_restriction_flag
        skip_bits(br, 1); // motion_vectors_over_pic_boundaries_flag
        read_ue_s(br);    // max_bytes_per_pic_denom
        read_ue_s(br);    // max_bits_per_mb_denom
        read_ue_s(br);    // log2_max_mv_length_horizontal
        read_ue_s(br);    // log2_max_mv_length_vertical
        read_ue_s(br);    // max_num_reorder_frames
        read_ue_s(br);    // max_dec_frame_buffering
    }
    return vui;
}

typedef struct {
    struct h264_sps sps;
    h264_result result;
} h264_sps_result;

sz_no_discard static inline h264_sps_result h264_parse_sps(sz_span sps_bytes) {
    h264_sps_result r = {0};

    r.sps.profile_idc = sps_bytes.ptr[0];
    r.sps.constraint_set_flags = sps_bytes.ptr[1];
    r.sps.level_idc = sps_bytes.ptr[2];

    bitreader bit_reader;
    br_init(&bit_reader, sps_bytes.ptr + 3, sps_bytes.len - 3);

#define ue_s() read_ue_s(&bit_reader);
#define se_s() read_se_s(&bit_reader);
#define u_s(n) read_u(&bit_reader, n);

    r.sps.seq_parameter_set_id = ue_s();

    switch (r.sps.profile_idc) {
    // (todo): use lookup table for clarity
    case 100:
    case 110:
    case 122:
    case 244:
    case 44:
    case 83:
    case 86:
    case 118:
    case 128:
    case 138:
    case 139:
    case 134:
    case 135:
        r.sps.chroma_format_idc = ue_s();
        if (r.sps.chroma_format_idc == 3)
            r.sps.separate_colour_plane_flag = u_s(1);

        r.sps.bit_depth_luma_minus8 = ue_s();
        r.sps.bit_depth_chroma_minus8 = ue_s();

        r.sps.qpprime_y_zero_transform_bypass_flag = u_s(1);
        r.sps.seq_scaling_matrix_present_flag = u_s(1);

        if (r.sps.seq_scaling_matrix_present_flag) {
            for (int i = 0; i < ((r.sps.chroma_format_idc != 3) ? 8 : 12); ++i) {
                r.sps.seq_scaling_list_present_flag[i] = u_s(1);
                if (r.sps.seq_scaling_list_present_flag[i]) {
                    if (i < 6)
                        r.sps.use_default_scaling_matrix_flag[i] =
                            scaling_list(&bit_reader, r.sps.scaling_list_4x4[i], 16);
                    else
                        r.sps.use_default_scaling_matrix_flag[i] =
                            scaling_list(&bit_reader, r.sps.scaling_list_8x8[i - 6], 64);
                }
            }
        }
        break;

    default:
        break;
    }

    r.sps.log2_max_frame_num_minus4 = ue_s();
    r.sps.pic_order_cnt_type = ue_s();

    if (r.sps.pic_order_cnt_type == 0) {
        r.sps.log2_max_pic_order_cnt_lsb_minus4 = ue_s();
    } else if (r.sps.pic_order_cnt_type == 1) {
        r.sps.delta_pic_order_always_zero_flag = u_s(1);
        r.sps.offset_for_non_ref_pic = se_s();
        r.sps.offset_for_top_to_bottom_field = se_s();
        r.sps.num_ref_frames_in_pic_order_cnt_cycle = ue_s();
    }

    if (r.sps.num_ref_frames_in_pic_order_cnt_cycle > 255) {
        r.result = SZ_H264_ERR_BAD_REF_FRAMES;
        return r;
    }

    for (u32 i = 0; i < r.sps.num_ref_frames_in_pic_order_cnt_cycle; ++i)
        r.sps.offset_for_ref_frame[i] = se_s();

    r.sps.max_num_ref_frames = ue_s();
    r.sps.gaps_in_frame_num_value_allowed_flag = u_s(1);
    r.sps.pic_width_in_mbs_minus1 = ue_s();
    r.sps.pic_height_in_map_units_minus1 = ue_s();
    r.sps.frame_mbs_only_flag = u_s(1);

    if (!r.sps.frame_mbs_only_flag)
        r.sps.mb_adaptive_frame_field_flag = u_s(1);

    r.sps.direct_8x8_inference_flag = u_s(1);
    r.sps.frame_cropping_flag = u_s(1);

    if (r.sps.frame_cropping_flag) {
        r.sps.frame_crop_left_offset = ue_s();
        r.sps.frame_crop_right_offset = ue_s();
        r.sps.frame_crop_top_offset = ue_s();
        r.sps.frame_crop_bottom_offset = ue_s();
    }

    r.sps.vui_parameters_present_flag = u_s(1);

    if (r.sps.vui_parameters_present_flag) {
        r.sps.vui = h264_vui_parameters(&bit_reader);
        r.sps.vui_present = true;
    }

    if (bit_reader.err) {
        r.result = SZ_H264_ERR_TRUNCATED;
        return r;
    }

#undef ue_s
#undef se_s
#undef u_s

    r.result = SZ_H264_OK;
    return r;
}

static void h264_vui_dump(const struct h264_vui *vui) {
    printf("========== H264 VUI ==========\n");

    printf("video_full_range_flag: %u\n", vui->video_full_range_flag);
    printf("colour_primaries: %u\n", vui->colour_primaries);
    printf("transfer_characteristics: %u\n", vui->transfer_characteristics);
    printf("matrix_coefficients: %u\n", vui->matrix_coefficients);

    printf("max_num_reorder_frames: %u\n", vui->max_num_reorder_frames);
    printf("max_dec_frame_buffering: %u\n", vui->max_dec_frame_buffering);
}

static void h264_sps_dump(const struct h264_sps *sps) {
    printf("========== H264 SPS ==========\n");

    printf("profile_idc: %u\n", sps->profile_idc);
    printf("constraint_set_flags: 0x%02X\n", sps->constraint_set_flags);
    printf("level_idc: %u\n", sps->level_idc);

    printf("seq_parameter_set_id: %u\n", sps->seq_parameter_set_id);

    printf("chroma_format_idc: %u\n", sps->chroma_format_idc);
    printf("separate_colour_plane_flag: %u\n", sps->separate_colour_plane_flag);

    printf("bit_depth_luma_minus8: %u\n", sps->bit_depth_luma_minus8);
    printf("bit_depth_chroma_minus8: %u\n", sps->bit_depth_chroma_minus8);

    printf("qpprime_y_zero_transform_bypass_flag: %u\n", sps->qpprime_y_zero_transform_bypass_flag);

    printf("seq_scaling_matrix_present_flag: %u\n", sps->seq_scaling_matrix_present_flag);

    for (int i = 0; i < 12; ++i) {
        printf("seq_scaling_list_present_flag[%d]: %u\n", i, sps->seq_scaling_list_present_flag[i]);
    }

    printf("log2_max_frame_num_minus4: %u\n", sps->log2_max_frame_num_minus4);
    printf("pic_order_cnt_type: %u\n", sps->pic_order_cnt_type);
    printf("log2_max_pic_order_cnt_lsb_minus4: %u\n", sps->log2_max_pic_order_cnt_lsb_minus4);

    printf("delta_pic_order_always_zero_flag: %u\n", sps->delta_pic_order_always_zero_flag);

    printf("offset_for_non_ref_pic: %d\n", sps->offset_for_non_ref_pic);
    printf("offset_for_top_to_bottom_field: %d\n", sps->offset_for_top_to_bottom_field);

    printf("num_ref_frames_in_pic_order_cnt_cycle: %u\n",
           sps->num_ref_frames_in_pic_order_cnt_cycle);

    for (u32 i = 0; i < sps->num_ref_frames_in_pic_order_cnt_cycle && i < 256; ++i) {
        printf("offset_for_ref_frame[%u]: %d\n", i, sps->offset_for_ref_frame[i]);
    }

    printf("max_num_ref_frames: %u\n", sps->max_num_ref_frames);
    printf("gaps_in_frame_num_value_allowed_flag: %u\n", sps->gaps_in_frame_num_value_allowed_flag);

    printf("pic_width_in_mbs_minus1: %u\n", sps->pic_width_in_mbs_minus1);
    printf("pic_height_in_map_units_minus1: %u\n", sps->pic_height_in_map_units_minus1);

    printf("frame_mbs_only_flag: %u\n", sps->frame_mbs_only_flag);
    printf("mb_adaptive_frame_field_flag: %u\n", sps->mb_adaptive_frame_field_flag);
    printf("direct_8x8_inference_flag: %u\n", sps->direct_8x8_inference_flag);

    printf("frame_cropping_flag: %u\n", sps->frame_cropping_flag);
    printf("frame_crop_left_offset: %u\n", sps->frame_crop_left_offset);
    printf("frame_crop_right_offset: %u\n", sps->frame_crop_right_offset);
    printf("frame_crop_top_offset: %u\n", sps->frame_crop_top_offset);
    printf("frame_crop_bottom_offset: %u\n", sps->frame_crop_bottom_offset);

    printf("vui_parameters_present_flag: %u\n", sps->vui_parameters_present_flag);
    printf("vui_present: %u\n", sps->vui_present);

    if (sps->vui_present)
        h264_vui_dump(&sps->vui);

    printf("==============================\n");
}

h264_result h264_get_frame(const struct rtp_packet *_Nonnull packet, h264_frame_t *frame) {
    // 96 = H264
    if (UNLIKELY((packet->payload_type != RTP_PT_H264)))
        return SZ_H264_INTERNAL_ERROR;

    sz_span payload = packet->payload;

    const u8 nal = *sz_span_first(payload);
    // this is forbidden bit must be 0
    if (UNLIKELY(((nal >> 7) & 0x01) != 0))
        return SZ_H264_INTERNAL_ERROR;

    // reference important idicator
    const u8 nri = (nal >> 5) & 0x3;
    const nal_unit_type_t nal_type = get_nal_type(nal);

    switch (nal_type) {
    case NAL_TYPE_SINGLE:
        if ((nal & 0x1f) == H264_NAL_SPS) {
            h264_sps_result sps_result =
                h264_parse_sps(sz_span_subspan(payload, 1, payload.len - 1));
            if (sps_result.result != SZ_H264_OK) {
                printf("failed to parse sps error = %s\n", h264_result_str(sps_result.result));
                return sps_result.result;
            }

            h264_sps_dump(&sps_result.sps);
        };

        if ((nal & 0x1f) == H264_NAL_PPS) {
            // handle pps
        }

        handle_nal_single(payload, frame);
        if (packet->marker) {
            // printf("END_SEQ=%u, TYPE=%u, M=%d TS=%u offset=%zu\n",
            //        packet->sequence_number,
            //        nal_type,
            //        packet->marker,
            //        packet->timestamp,
            //        sz_arena_pos(&frame->arena));
            break;
        };
        break;

    // Aggregation Packets
    case NAL_TYPE_STAP_A:
        printf("nal stap-a not implemented\n");
        return SZ_H264_NOT_IMPLEMENTED;

    case NAL_TYPE_FU_A:
        printf("nal fu-a not implemented\n");
        return SZ_H264_NOT_IMPLEMENTED;

    case NAL_TYPE_UNKNOWN:
    case NAL_UNDEFINED:
        return SZ_H264_INTERNAL_ERROR;
    }
    return SZ_H264_OK;
}
