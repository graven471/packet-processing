#include "reassembler.h"

#include <errno.h>
#include <rte_errno.h>
#include <rte_lcore.h>
#include <rte_mempool.h>

#include <asm-generic/errno-base.h>

#include "rtp/serial_number.h"

int rtp_reassembler_init(seq_num isn, uint32_t timestamp, size_t pool_size, size_t elem_size,
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

/*
 * seq_num -> 101 NAL = FU-A, S = 1, E = 0, payload = 1400-bytes
 * seq_num -> 103 NAL = FU-A, S = 0, E = 0, payload = 1200-bytes
 * seq_num -> 102 NAL = FU-A, S = 0, E = 1, payload = 500-bytes
 *
 * we have to handle this seq_num -> 103 then seq_num -> 102 i don't just wanna skip this
 * instead i wanna store 103 somewhere and when 102 arrives i merge both payloads
 * payload 102 and payload 103
 * where can i store it and how do i know it belongs where and can i avoid copy ?
 * so let's say i received 102 then i will check reorder buffer is 103 present if yes
 * then copy its payload after 102 otherwise continue
 */

int rtp_reassembler_append(struct rtp_reassembler *self, seq_num seq, sz_span payload) {
    if (seq_lte(self->last_seq_number, seq))
        return -EINVAL;

    // handle out of order like if last_seq_number is 100 and seq 102 we need to store 102 somewhere
    // then wait for 101 then put 101 and 102 into buffer

    seq_num expected;
    if (seq_add(self->last_seq_number, 1, &expected) != 0)
        return -errno;

    if (expected == seq) {
        // in order delivery
    }

    // handle out-of-order

    return 0;
}
