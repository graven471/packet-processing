#include <assert.h>
#include <fcntl.h>
#include <iso646.h>
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

#include "allocator/slab.h"
#include "dpdk/dpdk.h"
#include "h264/nal.h"
#include "io_uring/ring_init.h"
#include "rtp/parser.h"
#include "sz/arena.h"
#include "sz/ring.h"
#include "sz/span.h"
#include "vk/vk_init.h"

struct User {
    const char *name;
    uint8_t age;
};

[[maybe_unused]] static inline void *producer(void *arg) {
    [[maybe_unused]] struct sz_ring *ring = (struct sz_ring *)arg;
    return NULL;
}

typedef struct {
    uint16_t length;
    uint8_t msg_type;
    uint32_t timestamp;
    uint64_t order_ref;
    char side; // 'B' or 'S'
    uint32_t shares;
    char symbol[8];
    uint32_t price;
} __attribute__((packed)) itch_add_order_t;

// convert this project into:
// write arena mmap, pre-fault, huge pages etc
// write slab so i can allocate itch_add_order_t without fragmentation
// setup DPDK
// should i use raw io_uring or liburing (write uring abstraction)
// validate a packet, do stuffs add to ring buffer, io_uring fetches it writes
// to disk etc
// NIC → DPDK rx thread → parse → ring → io_uring thread → disk

int main(int argc, char **argv) {
    struct sz_ring *ring = ring_init(32);

    if (ring == NULL) {
        fprintf(stderr, "failed to init ring\n");
        return -1;
    }

    // 	pthread_t ring_thread;
    // 	pthread_create(&ring_thread, NULL, producer, ring);
    // 	pthread_join(ring_thread, NULL);
    //
    // 	struct User *user = (struct User *)ring->data;
    // 	user[write_index(ring)] = (struct User){ .name = "simple", .age = 21 };
    // 	ring_commit_write(ring);
    //
    // 	struct User *data = &user[read_index(ring)];
    // 	printf("name = \t %s, age = \t %d \n", data->name, data->age);
    // 	ring_commit_read(ring);
    //
    // 	ring_free(ring);

    struct sz_arena *arena = arena_init(256, true, true);
    if (arena == NULL) {
        fprintf(stderr, "arena init failed\n");
        return -1;
    }

    arena_destroy(arena);

    struct kmem_cache *cache = kmem_cache_create("itch_order", sizeof(itch_add_order_t));
    if (!cache) {
        fprintf(stderr, "failed to create cache");
        return -1;
    }

    itch_add_order_t *order = kmem_cache_alloc(cache);

    if (!order) {
        fprintf(stderr, "kmem alloc failed\n");
        return -1;
    }

    printf("addr = %lxlu\n", (uintptr_t)order);

    *order = (itch_add_order_t){.length = 20,
                                .msg_type = 10,
                                .side = 'B',
                                .shares = 30,
                                .symbol = {0},
                                .timestamp = 123424};

    printf("%d, %d, %c \n", order->length, order->msg_type, order->side);

    kmem_cache_free(cache, (void *)order);

    int retval = 0;

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
                                                          (int)rte_socket_id());

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

    constexpr uint16_t nb_pkts = 32;
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
    struct rte_mbuf *rx_pkts[nb_pkts];

    while (1) {
        uint16_t nb = rte_eth_rx_burst(config.port_id, 0, rx_pkts, nb_pkts);
        for (uint16_t i = 0; i < nb; ++i) {
            // this is the metadata
            const struct rte_mbuf *pkt = rx_pkts[i];
            // L2
            struct rte_ether_hdr *eth = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);

            // (todo): if ether type is RTE_ETHER_TYPE_VLAN (0x8100) then there is 4 byte VLAN
            // inserted before the real ether type in config we have RX_OFFLOAD_VLAN_STRIP so if its
            // true we can ignore otherwise we have to remove it manually skip non ipv4 header
            if (rte_be_to_cpu_16(eth->ether_type) != RTE_ETHER_TYPE_IPV4) [[clang::unlikely]]
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
            uint16_t rtp_len = rte_be_to_cpu_16(udp->dgram_len) - sizeof(struct rte_udp_hdr);

            sz_span rtp_span = sz_span_make(rtp_data, rtp_len);

            struct rtp_packet rtp_pkt = {0};
            if (rtp_parse(rtp_span, &rtp_pkt) != RTP_PARSE_OK)
                continue;

            h264_nal_parse(&rtp_pkt);

            // rte_pktmbuf_dump(stdout, pkt, pkt->pkt_len);
            // fflush(stdout);

            // (todo): do not free in hot path free later
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
