#pragma once

#include <stdint.h>

#include "rtp/parser.h"
#include "sz/arena.h"

#define RTP_PT_H264 96

typedef struct {
    uint32_t timestamp;
    struct sz_arena arena;
} h264_frame_t;

// refer to H264 ITU-T specs 7.3.2.1.1 Sequence parameter set data syntax
struct h264_sps {
    uint8_t profile_idc;                            /* u(8) */
    uint8_t constraint_set_flags;                   /* 6x u(1) */
    uint8_t level_idc;                              /* u(8) */
    uint32_t seq_parameter_set_id;                  /* ue(v) needs Exp-Golomb decoding */
    uint32_t chroma_format_idc;                     /* ue(v) */
    uint8_t separate_colour_plane_flag;             /* u(1) either 0 or 1 */
    uint32_t bit_depth_luma_minus8;                 /* ue(v) */
    uint32_t bit_depth_chroma_minus8;               /* ue(v) */
    uint8_t qpprime_y_zero_transform_bypass_flag;   /* u(1) */
    uint8_t seq_scaling_matrix_present_flag;        /* u(1) */
    uint8_t seq_scaling_list_present_flag[12];      /* u(1) */
    uint32_t log2_max_frame_num_minus4;             /* ue(v) */
    uint32_t pic_order_cnt_type;                    /* ue(v) */
    uint32_t log2_max_pic_order_cnt_lsb_minus4;     /* ue(v) */
    uint8_t delta_pic_order_always_zero_flag;       /* u(1) */
    int32_t offset_for_non_ref_pic;                 /* se(v) */
    int32_t offset_for_top_to_bottom_field;         /* se(v) */
    uint32_t num_ref_frames_in_pic_order_cnt_cycle; /* ue(v) */
    int32_t offset_for_ref_frame[256];              /* se(v) */
    uint32_t max_num_ref_frames;                    /* ue(v) */
    uint8_t gaps_in_frame_num_value_allowed_flag;   /* u(1) */
    uint32_t pic_width_in_mbs_minus1;               /* ue(v) */
    uint32_t pic_height_in_map_units_minus1;        /* ue(v) */
    uint8_t frame_mbs_only_flag;                    /* u(1) */
    uint8_t mb_adaptive_frame_field_flag;           /* u(1) */
    uint8_t direct_8x8_inference_flag;              /* u(1) */
    uint8_t frame_cropping_flag;                    /* u(1) */
    uint32_t frame_crop_left_offset;                /* ue(v) */
    uint32_t frame_crop_right_offset;               /* ue(v) */
    uint32_t frame_crop_top_offset;                 /* ue(v) */
    uint32_t frame_crop_bottom_offset;              /* ue(v) */
    uint8_t vui_parameters_present_flag;            /* u(1) */
};

// refer to H264 ITU-T specs 7.3.2.2 Picture parameter set RBSP syntax
struct h264_pps {
    uint32_t pic_parameter_set_id;                        /* ue(v) */
    uint32_t seq_parameter_set_id;                        /* ue(v) */
    uint8_t entropy_coding_mode_flag;                     /* u(1) */
    uint8_t bottom_field_pic_order_in_frame_present_flag; /* u(1) */
    uint32_t num_slice_groups_minus1;                     /* ue(v) */
    uint32_t slice_group_map_type;                        /* ue(v) */
    uint8_t slice_group_change_direction_flag;            /* u(1) */
    uint32_t slice_group_change_rate_minus1;              /* ue(v) */
    uint32_t pic_size_in_map_units_minus1;                /* ue(v) */
    /* i am not storing slice_group_id[i] cause i am not decoding FMO stuffs */
    uint32_t num_ref_idx_l0_default_active_minus1;  /* ue(v) */
    uint32_t num_ref_idx_l1_default_active_minus1;  /* ue(v) */
    uint8_t weighted_pred_flag;                     /* u(1) */
    uint8_t weighted_bipred_idc;                    /* u(2) */
    int32_t pic_init_qp_minus26;                    /* se(v) */
    int32_t pic_init_qs_minus26;                    /* se(v) */
    int32_t chroma_qp_index_offset;                 /* se(v) */
    uint8_t deblocking_filter_control_present_flag; /* u(1) */
    uint8_t constrained_intra_pred_flag;            /* u(1) */
    uint8_t redundant_pic_cnt_present_flag;         /* u(1) */
    uint8_t transform_8x8_mode_flag;                /* u(1) */
    uint8_t pic_scaling_matrix_present_flag;        /* u(1) */
    uint8_t pic_scaling_list_present_flag[12];      /* u(1) */
    int32_t second_chroma_qp_index_offset;          /* se(v) */
};

struct h264_decoder {
    struct h264_sps sps[32];
    struct h264_pps pps[256];

    // other decoder stuffs
};

typedef enum {
    SZ_H264_OK = 0,
    SZ_H264_NOT_IMPLEMENTED,
    SZ_H264_INTERNAL_ERROR,
    SZ_H264_MEMORY_ERROR,
} h264_result;

h264_result h264_prepare_for_frame(size_t payload_capacity, h264_frame_t *frame);
h264_result h264_get_frame(const struct rtp_packet *_Nonnull packet, h264_frame_t *frame);
