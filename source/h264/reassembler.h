#pragma once

#include <rte_mempool.h>
#include <stdint.h>

#include "sz/span.h"

struct rtp_reassembler {
    uint32_t timestamp;
    uint16_t last_seq_number;    /* (todo): use modulo arithmetic */
    struct rte_mempool *mempool; /* (todo): later replace it with my slab */
};

int rtp_reassembler_init(uint16_t isn, uint32_t timestamp, size_t pool_size, size_t elem_size,
                         struct rtp_reassembler **_Nonnull out);

int rtp_reassembler_append(struct rtp_reassembler *self, uint16_t seq, sz_span payload);
