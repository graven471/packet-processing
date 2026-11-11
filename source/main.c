#include "allocator/slab.h"
#include "dpdk/dpdk.h"
#include "io_uring/ring_init.h"
#include "sz/arena.h"
#include "sz/ring.h"
#include <assert.h>
#include <iso646.h>
#include <linux/io_uring.h>
#include <pthread.h>
#include <rte_build_config.h>
#include <rte_common.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_mbuf_core.h>
#include <rte_mempool.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <sys/queue.h>

#include <rte_debug.h>
#include <rte_eal.h>
#include <rte_launch.h>
#include <rte_lcore.h>
#include <rte_memory.h>
#include <rte_per_lcore.h>

#include <linux/io_uring.h>

struct User {
	const char *name;
	uint8_t age;
};

[[maybe_unused]] static inline void *producer(void *arg)
{
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

int main(int argc, char **argv)
{
	struct sz_ring *ring = ring_init(32);

	if (ring == NULL) {
		fprintf(stderr, "failed to init ring\n");
		return -1;
	}

	pthread_t ring_thread;
	pthread_create(&ring_thread, NULL, producer, ring);
	pthread_join(ring_thread, NULL);

	struct User *user = (struct User *)ring->data;
	user[write_index(ring)] = (struct User){ .name = "simple", .age = 21 };
	ring_commit_write(ring);

	struct User *data = &user[read_index(ring)];
	printf("name = \t %s, age = \t %d \n", data->name, data->age);
	ring_commit_read(ring);

	ring_free(ring);

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

	if (!order) [[clang::unlikely]] {
		fprintf(stderr, "kmem alloc failed");
		return -1;
	}

	printf("addr = %lxlu\n", (uintptr_t)order);

	*order = (itch_add_order_t){
		.length = 20, .msg_type = 10, .side = 'B', .shares = 30, .symbol = { 0 }, .timestamp = 123424
	};

	printf("%d, %d, %c \n", order->length, order->msg_type, order->side);

	kmem_cache_free(cache, (void *)order);

	int retval;

	// ================== INIT Lord of Rings ===========

	struct io_uring_params params = { .flags = IORING_SETUP_SQPOLL, .sq_thread_idle = 0 };

	struct sz_uring_context *context = sz_uring_create(32, &params);

	if (context == nullptr) {
		fprintf(stderr, "%s\n", strerror(-retval));
		return -1;
	}

	// ================== INIT DPDK ==================

	retval = init_eal(argc, argv);
	if (retval != 0)
		rte_exit(EXIT_FAILURE, "Eal Initialization failed: %s\n", rte_strerror(-retval));

	// create a rx pool
	struct rte_mempool *rx_pool =
		rte_pktmbuf_pool_create("RX_POOL", 8192, 256, 0, RTE_MBUF_DEFAULT_BUF_SIZE, (int)rte_socket_id());

	if (rx_pool == nullptr)
		rte_exit(EXIT_FAILURE, "failed to create rx pool error: %s\n", rte_strerror(rte_errno));

	struct dpdk_port_config config = { .tx_flags = TX_OFFLOAD_IPV4_CKSUM | TX_OFFLOAD_TCP_CKSUM |
						       TX_OFFLOAD_UDP_CKSUM,
					   .rx_flags = RX_OFFLOAD_VLAN_STRIP | RX_OFFLOAD_IPV4_CKSUM,
					   .nb_rx_queues = 1,
					   .nb_tx_queues = 1,
					   .nb_rx_descriptors = 128,
					   .nb_tx_descriptors = 128,
					   .rx_mempool = rx_pool };

	retval = configure_port(&config);
	if (retval != 0) {
		rte_mempool_free(rx_pool);
		rte_exit(EXIT_FAILURE, "Port initialization failed: %s\n", rte_strerror(-retval));
	}

	constexpr uint16_t nb_pkts = 32;
	struct rte_mbuf *rx_pkts[nb_pkts];

	while (1) {
		uint16_t nb = rte_eth_rx_burst(config.port_id, 0, rx_pkts, nb_pkts);

		for (uint16_t i = 0; i < nb; ++i) {
			// this is the metadata
			const struct rte_mbuf *rx_pkt = rx_pkts[nb];
			// this points to actual packet buffer but there is a headroom and tailroom
			// this points to the first byte of that buffer
			char *buf_addr = rx_pkt->buf_addr;
			// this points to the actual packet
			uint64_t packet_start = (uint64_t)((char *)rx_pkt->buf_addr) + rx_pkt->ol_flags;
			// internally it does: ((t)(void *)((char *)(m)->buf_addr + (m)->data_off + (o)))
			const uint8_t *data = rte_pktmbuf_mtod(rx_pkt, const uint8_t *);
			rte_pktmbuf_free(rx_pkts[i]);
		}
	}

	rte_mempool_free(rx_pool);

	// (todo): why i am getting seg fault when i call these function
	// rte_eth_dev_stop(config.port_id);
	// rte_eth_dev_close(config.port_id);
	// rte_eal_cleanup();

	return EXIT_SUCCESS;
}
