#pragma once

#include <rte_mempool.h>
#include <stdint.h>

struct rtp_reassembler {
    uint32_t timestamp;
    uint16_t seq_number;         /* (todo): use modulo arithmetic */
    struct rte_mempool *mempool; /* (todo): later replace it with my slab */
};

struct rtp_reassembler rtp_reassembler_init(uint16_t isn, uint32_t timestamp, size_t pool_size);
