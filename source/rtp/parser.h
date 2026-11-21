#pragma once

#include <stddef.h>
#include <stdint.h>

#define RTP_HDR_LEN 12
#define RTP_PT_H264 96

bool rtp_parse(const uint8_t *_Nonnull rtp_buf, size_t len);
