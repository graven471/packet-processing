#include "reassembler.h"

#include <rte_errno.h>
#include <rte_lcore.h>
#include <rte_mempool.h>

#include <asm-generic/errno-base.h>

int rtp_reassembler_init(uint16_t isn, uint32_t timestamp, size_t pool_size, size_t elem_size,
                         struct rtp_reassembler **out) {
    (*out)->mempool = rte_mempool_create("RTP_REASSEMBLER",
                                         pool_size,
                                         elem_size,
                                         64,
                                         0,
                                         nullptr,
                                         nullptr,
                                         nullptr,
                                         nullptr,
                                         rte_socket_id(),
                                         RTE_MEMPOOL_F_NO_SPREAD);

    if ((*out)->mempool == nullptr)
        return -rte_errno;

    (*out)->last_seq_number = isn;
    (*out)->timestamp = timestamp;
    return 0;
}

int rtp_reassembler_append(struct rtp_reassembler *self, uint16_t seq, sz_span payload) {
    if (seq <= self->last_seq_number)
        return -EINVAL;

    // handle out of order like if last_seq_number is 100 and seq 102 we need to store 102 somewhere
    // then wait for 101 then put 101 and 102 into buffer

    return 0;
}
