#pragma once

#include "rtp/parser.h"

#define RTP_PT_H264 96

void h264_nal_parse(struct rtp_packet *_Nonnull packet);
