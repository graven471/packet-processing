#pragma once

#include <rte_mempool.h>
#include <stdint.h>

#include "rtp/serial_number.h"
#include "sz/span.h"

struct rtp_reassembler {
    uint32_t timestamp;
    seq_num last_seq_number;
    struct rte_mempool *mempool; /* (todo): later replace it with my slab */
    struct rte_ring *ring;
};

int rtp_reassembler_init(seq_num isn, uint32_t timestamp, size_t pool_size, size_t elem_size,
                         struct rtp_reassembler **_Nonnull out);

int rtp_reassembler_append(struct rtp_reassembler *self, seq_num seq, sz_span payload);
