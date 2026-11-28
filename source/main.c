#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <rte_build_config.h>
#include <rte_byteorder.h>
#include <rte_common.h>
#include <rte_debug.h>
#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip4.h>
#include <rte_launch.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_mbuf_core.h>
#include <rte_memory.h>
#include <rte_mempool.h>
#include <rte_per_lcore.h>
#include <rte_udp.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/io_uring.h>
#include <netinet/in.h>
#include <sys/cdefs.h>
#include <sys/mman.h>
#include <sys/queue.h>
#include <sys/types.h>

#include "dpdk/dpdk.h"
#include "h264/h264.h"
#include "io_uring/ring_init.h"
#include "rtp/parser.h"
#include "rtp/serial_number.h"
#include "sz/arena.h"
#include "sz/macros.h"
#include "sz/span.h"
#include "sz/sz_types.h"
#include "sz/sz_vector.h"
#include "vk/vk_init.h"

typedef struct {
    seq_num prev_seq_num;
    seq_num expected;
} out_of_order_t;

int main(int argc, char **argv) {
    int retval = 0;

    struct sz_arena_config cfg = {.capacity = 4096, .flags = ARENA_FLAG_NONE, .pre_fault = true};
    struct sz_arena arena;
    retval = sz_arena_init(&cfg, &arena);

    if (retval != 0)
        return -1;

    // ================== INIT Lord of Rings ===========
    struct io_uring_params params = {.flags = IORING_SETUP_SQPOLL, .sq_thread_idle = 500};
    struct sz_uring_context *uring = sz_uring_create(128, &params);

    if (uring == nullptr) {
        fprintf(stderr, "%s\n", strerror(-retval));
        return -1;
    }

    // =================== init vulkan video ===================
    struct vk_context *vk = vk_context_init();

    if (vk == nullptr) {
        sz_uring_destroy(uring);
        return -1;
    }

    // ================== INIT DPDK ==================
    retval = init_eal(argc, argv);
    if (retval != 0)
        rte_exit(EXIT_FAILURE, "Eal Initialization failed: %s\n", rte_strerror(-retval));

    // create a rx pool
    struct rte_mempool *rx_pool = rte_pktmbuf_pool_create("RX_POOL",
                                                          8192,
                                                          256,
                                                          0,
                                                          RTE_MBUF_DEFAULT_BUF_SIZE,
                                                          rte_socket_id());

    if (rx_pool == nullptr)
        rte_exit(EXIT_FAILURE, "failed to create rx pool error: %s\n", rte_strerror(rte_errno));

    struct dpdk_port_config config = {.tx_flags = TX_OFFLOAD_IPV4_CKSUM | TX_OFFLOAD_TCP_CKSUM
                                                  | TX_OFFLOAD_UDP_CKSUM,
                                      .rx_flags = RX_OFFLOAD_VLAN_STRIP | RX_OFFLOAD_IPV4_CKSUM,
                                      .nb_rx_queues = 1,
                                      .nb_tx_queues = 1,
                                      .nb_rx_descriptors = 128,
                                      .nb_tx_descriptors = 128,
                                      .rx_mempool = rx_pool};

    retval = configure_port(&config);
    if (retval != 0) {
        rte_mempool_free(rx_pool);
        rte_exit(EXIT_FAILURE, "Port initialization failed: %s\n", rte_strerror(-retval));
    }

    h264_frame_t h264_frame = {0};
    if (h264_prepare_for_frame(2097152, &h264_frame) != SZ_H264_OK)
        exit(1);

    sz_vector vec = {0};
    if (sz_vector_init_with_capacity(sizeof(uint32_t), 256, &vec) != SZ_VECTOR_OK)
        exit(1);

    sz_vector_deinit(&vec);

    constexpr u16 nb_pkts = 32;
    /*
     * struct rte_mubf is just a metadata actual data lives at rte_mbuf.buf_addr
     * buf_addr is that starting address there is actually headroom then packet then tailroom
     * we do buf_addr + rte_mbuf.data_off to get to the start of the actual packet
     * but there is only one allocation for struct rte_mbuf and data
     * for UDP packet the layout is following:
     *  0x1000
     *  _______________________________________
     * | ethernet header 14 bytes              |
     * | mac{src, addr} and ether_type         |
     * |_______________________________________|
     *  0x100E
     * |----------------------------------------|
     * | ipv4 header 20 bytes ignoring options  |
     * |________________________________________|
     * 0x1022
     * |----------------------------------------|
     * |           udp header 8 bytes           |
     * |________________________________________|
     * 0x102A
     * |----------------------------------------|
     * |           payload n bytes              |
     * |________________________________________|
     */

    out_of_order_t out_of_order_test = {0};

    struct rte_mbuf *rx_pkts[nb_pkts];
    bool b_is_initialized = false;

    /*
     * what is the problem ?
     * i need an storage to store the one frame payload
     * it should not reset until the frame is completed and i used it
     */

    while (1) {
        u16 nb = rte_eth_rx_burst(config.port_id, 0, rx_pkts, nb_pkts);
        for (u16 i = 0; i < nb; ++i) {
            // this is the metadata
            const struct rte_mbuf *pkt = rx_pkts[i];
            // L2
            struct rte_ether_hdr *eth = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);

            if (UNLIKELY(rte_be_to_cpu_16(eth->ether_type) != RTE_ETHER_TYPE_IPV4))
                continue;

            // rte_ether_hdr is always 14 bytes fixed
            // L3
            const struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)(eth + 1);
            uint8_t protocol = ip->next_proto_id;

            // skip non-UDP protocol
            if (protocol != IPPROTO_UDP)
                continue;

            // L4
            const struct rte_udp_hdr *udp = (struct rte_udp_hdr *)((uint8_t *)ip + ip->ihl * 4);

            if (rte_be_to_cpu_16(udp->dst_port) != 9000)
                continue;

            const uint8_t *rtp_data = rte_pktmbuf_mtod(pkt, const uint8_t *)
                                      + sizeof(struct rte_ether_hdr) + ip->ihl * 4
                                      + sizeof(struct rte_udp_hdr);
            const u16 rtp_len = rte_be_to_cpu_16(udp->dgram_len) - sizeof(struct rte_udp_hdr);

            sz_span rtp_span = sz_span_make(rtp_data, rtp_len);

            struct rtp_packet rtp_pkt;
            if (UNLIKELY(rtp_parse(rtp_span, &rtp_pkt) != RTP_PARSE_OK))
                continue;

            if (UNLIKELY(b_is_initialized && rtp_pkt.sequence_number != out_of_order_test.expected))
                // handle OoD
                printf("missing packet prev = %u, expected = %u, got = %u\n",
                       out_of_order_test.prev_seq_num,
                       out_of_order_test.expected,
                       rtp_pkt.sequence_number);

            // wip
            if (h264_get_frame(&rtp_pkt, &h264_frame) != SZ_H264_OK)
                continue;

            if (rtp_pkt.marker) {
                // end of the frame
                // use the frame
                // ........
                // reset the arena
                sz_arena_reset(&h264_frame.arena);
            }

            if (UNLIKELY(!b_is_initialized))
                b_is_initialized = true;

            out_of_order_test.prev_seq_num = rtp_pkt.sequence_number;
            seq_add(out_of_order_test.prev_seq_num, 1, &out_of_order_test.expected);

            // rte_pktmbuf_dump(stdout, pkt, pkt->pkt_len);
            // fflush(stdout);

            rte_pktmbuf_free(rx_pkts[i]);
        }
    }

    rte_mempool_free(rx_pool);
    vk_context_destroy(vk);

    // (todo): why i am getting seg fault when i call these function
    // rte_eth_dev_stop(config.port_id);
    // rte_eth_dev_close(config.port_id);
    // rte_eal_cleanup();

    return EXIT_SUCCESS;
}
