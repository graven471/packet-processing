#pragma once

#include <stdint.h>

#include "rtp/parser.h"
#include "sz/arena.h"

#define RTP_PT_H264 96

struct h264_context {
    struct sz_arena arena;
    // add other stuffs
};

typedef struct {
    struct sz_arena arena;
    uint32_t timestamp;
} h264_frame_t;

typedef enum {
    SZ_H264_OK = 0,
    SZ_H264_NOT_IMPLEMENTED,
    SZ_H264_INTERNAL_ERROR,
} h264_result;

h264_result h264_init(struct h264_context *context);
h264_result h264_get_frame(const struct rtp_packet *_Nonnull packet, h264_frame_t *frame);
