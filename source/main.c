#include "allocator/slab.h"
#include "dpdk/dpdk.h"
#include "sz/arena.h"
#include "sz/list.h"
#include "sz/ring.h"
#include <assert.h>
#include <iso646.h>
#include <pthread.h>
#include <rte_build_config.h>
#include <rte_mbuf.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
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

struct process {
	const char *name; // offset = 0, size = 8
	uint32_t pid; // offset = 8, size = 4,  4 padding
	struct list_head node; // offset = 16
};

static int lcore_hello(__rte_unused void *arg)
{
	unsigned lcore_id;
	lcore_id = rte_lcore_id();
	printf("hello from core %u\n", lcore_id);
	return 0;
}

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

	// test
	SZ_LIST_HEAD(process_head);

	struct process a = { .pid = 10, .name = "xwayland" };
	struct process b = { .pid = 20, .name = "swaybg" };

	list_add_tail(&a.node, &process_head);
	list_add(&b.node, &process_head);

	struct process *pos;

	list_for_each_entry (pos, &process_head, node) {
		printf("PID: %d, Name: %s\n", pos->pid, pos->name);
	}

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

	// ================== testing DPDK ==================
	learn();

	return 0;
}
