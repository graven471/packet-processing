#pragma once

#include <stdint.h>

#include "rtp/parser.h"
#include "sz/arena.h"
#include "sz/sz_types.h"

#define RTP_PT_H264 96

typedef struct {
    u32 timestamp;
    struct sz_arena arena;
} h264_frame_t;

struct h264_vui {
    u8 video_full_range_flag;    /* u(1) */
    u8 colour_primaries;         /* u(1) */
    u8 transfer_characteristics; /* u(1) */
    u8 matrix_coefficients;      /* u(1) */
    u32 max_num_reorder_frames;  /* ue(v) */
    u32 max_dec_frame_buffering; /* ue(v) */
};

struct h264_hrd_parameters {
    u32 cpb_cnt_minus1;                         /* ue(v) */
    u8 bit_rate_scale;                          /* u(4) */
    u8 cpb_size_scale;                          /* u(4) */
    u32 bit_rate_value_minus1[32];              /* ue(v) */
    u32 cpb_size_value_minus1[32];              /* ue(v) */
    u8 cbr_flag[32];                            /* u(1) */
    u8 initial_cpb_removal_delay_length_minus1; /* u(5) */
    u8 cpb_removal_delay_length_minus1;         /* u(5) */
    u8 dpb_output_delay_length_minus1;          /* u(5) */
    u8 time_offset_length;                      /* u(5) */
};

// refer to H264 ITU-T specs 7.3.2.1.1 Sequence parameter set data syntax
struct h264_sps {
    u8 profile_idc;                          /* u(8) */
    u8 constraint_set_flags;                 /* 6x u(1) */
    u8 level_idc;                            /* u(8) */
    u32 seq_parameter_set_id;                /* ue(v) needs Exp-Golomb decoding */
    u32 chroma_format_idc;                   /* ue(v) */
    u8 separate_colour_plane_flag;           /* u(1) either 0 or 1 */
    u32 bit_depth_luma_minus8;               /* ue(v) */
    u32 bit_depth_chroma_minus8;             /* ue(v) */
    u8 qpprime_y_zero_transform_bypass_flag; /* u(1) */
    u8 seq_scaling_matrix_present_flag;      /* u(1) */
    u8 scaling_list_4x4[6][16];
    u8 scaling_list_8x8[6][64];
    bool use_default_scaling_matrix_flag[12];
    u8 seq_scaling_list_present_flag[12];      /* u(1) */
    u32 log2_max_frame_num_minus4;             /* ue(v) */
    u32 pic_order_cnt_type;                    /* ue(v) */
    u32 log2_max_pic_order_cnt_lsb_minus4;     /* ue(v) */
    u8 delta_pic_order_always_zero_flag;       /* u(1) */
    i32 offset_for_non_ref_pic;                /* se(v) */
    i32 offset_for_top_to_bottom_field;        /* se(v) */
    u32 num_ref_frames_in_pic_order_cnt_cycle; /* ue(v) */
    i32 offset_for_ref_frame[256];             /* se(v) */
    u32 max_num_ref_frames;                    /* ue(v) */
    u8 gaps_in_frame_num_value_allowed_flag;   /* u(1) */
    u32 pic_width_in_mbs_minus1;               /* ue(v) */
    u32 pic_height_in_map_units_minus1;        /* ue(v) */
    u8 frame_mbs_only_flag;                    /* u(1) */
    u8 mb_adaptive_frame_field_flag;           /* u(1) */
    u8 direct_8x8_inference_flag;              /* u(1) */
    u8 frame_cropping_flag;                    /* u(1) */
    u32 frame_crop_left_offset;                /* ue(v) */
    u32 frame_crop_right_offset;               /* ue(v) */
    u32 frame_crop_top_offset;                 /* ue(v) */
    u32 frame_crop_bottom_offset;              /* ue(v) */
    u8 vui_parameters_present_flag;            /* u(1) */
    bool vui_present;
    struct h264_vui vui;
};

// refer to H264 ITU-T specs 7.3.2.2 Picture parameter set RBSP syntax
struct h264_pps {
    u32 pic_parameter_set_id;                        /* ue(v) */
    u32 seq_parameter_set_id;                        /* ue(v) */
    u8 entropy_coding_mode_flag;                     /* u(1) */
    u8 bottom_field_pic_order_in_frame_present_flag; /* u(1) */
    u32 num_slice_groups_minus1;                     /* ue(v) */
    u32 slice_group_map_type;                        /* ue(v) */
    u8 slice_group_change_direction_flag;            /* u(1) */
    u32 slice_group_change_rate_minus1;              /* ue(v) */
    u32 pic_size_in_map_units_minus1;                /* ue(v) */
    /* i am not storing slice_group_id[i] cause i am not decoding FMO stuffs */
    u32 num_ref_idx_l0_default_active_minus1;  /* ue(v) */
    u32 num_ref_idx_l1_default_active_minus1;  /* ue(v) */
    u8 weighted_pred_flag;                     /* u(1) */
    u8 weighted_bipred_idc;                    /* u(2) */
    i32 pic_init_qp_minus26;                   /* se(v) */
    i32 pic_init_qs_minus26;                   /* se(v) */
    i32 chroma_qp_index_offset;                /* se(v) */
    u8 deblocking_filter_control_present_flag; /* u(1) */
    u8 constrained_intra_pred_flag;            /* u(1) */
    u8 redundant_pic_cnt_present_flag;         /* u(1) */
    u8 transform_8x8_mode_flag;                /* u(1) */
    u8 pic_scaling_matrix_present_flag;        /* u(1) */
    u8 pic_scaling_list_present_flag[12];      /* u(1) */
    i32 second_chroma_qp_index_offset;         /* se(v) */
};

struct h264_decoder {
    struct h264_sps sps[32];
    struct h264_pps pps[256];

    // other decoder stuffs
};

typedef enum {
    SZ_H264_UNSET = 0, // zero-init default, never returned on purpose
    SZ_H264_OK,
    SZ_H264_NOT_IMPLEMENTED,
    SZ_H264_INTERNAL_ERROR,
    SZ_H264_MEMORY_ERROR,

    SZ_H264_ERR_TRUNCATED,           // bitreader ran off the end
    SZ_H264_ERR_BAD_NAL,             // forbidden_zero_bit set, or wrong nal_unit_type
    SZ_H264_ERR_OUT_OF_RANGE,        // sps_id > 31, log2_* > 12, cpb_cnt_minus1 > 31, ...
    SZ_H264_ERR_UNSUPPORTED_PROFILE, // not 66/77/88/100 etc.
    SZ_H264_ERR_UNSUPPORTED_CHROMA,  // 4:2:2 / 4:4:4 / separate colour planes
    SZ_H264_ERR_UNSUPPORTED_BIT_DEPTH,
    SZ_H264_ERR_UNSUPPORTED_SCALING, // seq_scaling_matrix_present_flag with no parser yet
    SZ_H264_ERR_BAD_REF_FRAMES,      // num_ref_frames_in_pic_order_cnt_cycle > 255, or
                                     // max_num_ref_frames too big
    SZ_H264_ERR_BAD_DIMENSIONS,      // zero size, crop larger than the picture
    SZ_H264_ERR_BAD_VUI,
    SZ_H264_ERR_MISSING_PARAMETER_SET, // slice references an SPS/PPS id you never received
} h264_result;

static inline const char *h264_result_str(h264_result r) {
    switch (r) {
#define C(x)                                                                                       \
    case x:                                                                                        \
        return #x;
        C(SZ_H264_UNSET)
        C(SZ_H264_OK)
        C(SZ_H264_NOT_IMPLEMENTED)
        C(SZ_H264_INTERNAL_ERROR)
        C(SZ_H264_MEMORY_ERROR)
        C(SZ_H264_ERR_TRUNCATED)
        C(SZ_H264_ERR_BAD_NAL)
        C(SZ_H264_ERR_OUT_OF_RANGE)
        C(SZ_H264_ERR_UNSUPPORTED_PROFILE)
        C(SZ_H264_ERR_UNSUPPORTED_CHROMA)
        C(SZ_H264_ERR_UNSUPPORTED_BIT_DEPTH)
        C(SZ_H264_ERR_UNSUPPORTED_SCALING)
        C(SZ_H264_ERR_BAD_REF_FRAMES)
        C(SZ_H264_ERR_BAD_DIMENSIONS)
        C(SZ_H264_ERR_BAD_VUI)
        C(SZ_H264_ERR_MISSING_PARAMETER_SET)
#undef C
    }
    return "SZ_H264_UNKNOWN";
}

h264_result h264_prepare_for_frame(size_t payload_capacity, h264_frame_t *frame);
h264_result h264_get_frame(const struct rtp_packet *_Nonnull packet, h264_frame_t *frame);
