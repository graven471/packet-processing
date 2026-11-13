#include "allocator/slab.h"
#include "dpdk/dpdk.h"
#include "io_uring/ring_init.h"
#include "io_uring/ring_ops.h"
#include "io_uring/syscall.h"
#include "sz/arena.h"
#include "sz/ring.h"
#include <assert.h>
#include <fcntl.h>
#include <iso646.h>
#include <linux/io_uring.h>
#include <netinet/in.h>
#include <pthread.h>
#include <rte_build_config.h>
#include <rte_byteorder.h>
#include <rte_common.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip4.h>
#include <rte_mbuf.h>
#include <rte_mbuf_core.h>
#include <rte_mempool.h>
#include <rte_udp.h>
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

	int retval = 0;

	// ================== INIT Lord of Rings ===========

	struct io_uring_params params = { .flags = IORING_SETUP_SQPOLL, .sq_thread_idle = 500 };
	struct sz_uring_context *context = sz_uring_create(128, &params);

	if (context == nullptr) {
		fprintf(stderr, "%s\n", strerror(-retval));
		return -1;
	}

	// =========== temp code =================
	constexpr uint32_t sqes_size = 8;
	struct io_uring_sqe *sqes[sqes_size];

	for (uint32_t i = 0; i < sqes_size; ++i) {
		sqes[i] = sz_uring_get_sqe(context);
		if (sqes[i] == nullptr) {
			fprintf(stderr, "failed to fetch sqe at index %u\n", i);
			sz_uring_destroy(context);
			return -1;
		}
		sqes[i]->opcode = IORING_OP_NOP;
		sqes[i]->fd = -1;
		sqes[i]->user_data = 0xdeadbeef;
		printf("sqe[%u] addr = %p\n", i, sqes[i]);
	}

	int n = sz_uring_submit(context);

	if (n < 0) {
		fprintf(stderr, "failed to submit sqe(s) %s\n", strerror(-n));
		sz_uring_destroy(context);
		return -1;
	}

	printf("submitted %d sqes\n", n);
	printf("pending is %u\n", context->__cached_sq_tail);

	sz_uring_enter(context->fd, 0, 1, IORING_ENTER_GETEVENTS, nullptr);

	for (uint32_t i = 0; i < sqes_size; ++i) {
		struct io_uring_cqe *cqes = sz_uring_cq_peek(context);
		if (cqes == nullptr) {
			printf("ring is empty\n");
			continue;
		}
		printf("cqe[%u] user_data = %llx res = %d\n", i, cqes[i].user_data, cqes[i].res);
	}

	n = sz_uring_cq_seen(context);
	printf("seen = %d\n", n);

	struct io_uring_sqe *sqe = sz_uring_prep_openat(context, AT_FDCWD, "meson.build", O_RDONLY, 0644);

	if (sqe == nullptr)
		return -1;

	sqe->user_data = 0xffdd;

	sz_uring_submit(context);

	struct io_uring_cqe *cqe = sz_uring_cq_peek_wait(context);

	char buf[4096];

	if (cqe == nullptr)
		return -1;

	if (cqe->user_data == 0xffdd) {
		sz_uring_cq_seen(context);
		if (cqe->res < 0) {
			printf("openat failed %s\n", strerror(-cqe->res));
			return -1;
		}
		sqe = sz_uring_prep_read(context, cqe->res, 0, buf, 4096);

		if (sqe == nullptr)
			return -1;

		sqe->user_data = 0x1010;
		sz_uring_submit(context);
	}

	cqe = sz_uring_cq_peek_wait(context);

	if (cqe != nullptr) {
		sz_uring_cq_seen(context);

		if (cqe->res > 0) {
			printf("%.*s", cqe->res, buf);
		}
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

			// (todo): if ether type is RTE_ETHER_TYPE_VLAN (0x8100) then there is 4 byte VLAN inserted
			// before the real ether type in config we have RX_OFFLOAD_VLAN_STRIP so if its true we can
			// ignore otherwise we have to remove it manually
			// skip non ipv4 header
			if (rte_be_to_cpu_16(eth->ether_type) != RTE_ETHER_TYPE_IPV4)
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

			rte_pktmbuf_dump(stdout, pkt, pkt->pkt_len);
			fflush(stdout);

			// (todo): do not free in hot path free later
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
