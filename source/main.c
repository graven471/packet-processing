#define _GNU_SOURCE

#include <pthread.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>

#define ALIGN_UP(x, align) (((x) + (align) - 1) & ~((align) - 1))

__attribute__((always_inline)) bool is_power_of_two(int n)
{
	return (n > 0) && ((n & (n - 1)) == 0);
}

alignas(64) struct CachedData {
	_Atomic uintptr_t value;
};

struct sz_ring {
	// todo: align 64 to avoid false sharing
	_Atomic uint32_t head;
	_Atomic uint32_t tail;

	size_t capacity;
	uint32_t mask;
	void *data;
};

struct sz_ring *ring_init(size_t capacity)
{
	if (!is_power_of_two(capacity))
		return NULL;

	struct sz_ring *ring = malloc(sizeof(struct sz_ring));

	if (ring == NULL) {
		perror("malloc");
		return NULL;
	}

	memset(ring, 0, sizeof(struct sz_ring));

	void *storage = mmap(NULL, capacity, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (storage == MAP_FAILED) {
		perror("mmap");
		free(ring);
		return NULL;
	}

	ring->capacity = capacity;
	ring->data = storage;
	ring->mask = (uint32_t)capacity - 1;

	return ring;
}

static void ring_free(struct sz_ring *ring)
{
	munmap(ring->data, ring->capacity);
	free(ring);
}

inline bool is_empty(const struct sz_ring *ring)
{
	// consumer will call this so we need proper sync on tail
	// and for head we can just use relaxed since producer is not gonna use this

	// this sync with producer release on ring->tail
	uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_acquire);
	uint32_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);

	return head == tail;
}

inline bool is_full(const struct sz_ring *ring)
{
	// producer will use this before writing so we need proper sync on head
	// this sync with consumer write with release
	uint32_t head = atomic_load_explicit(&ring->head, memory_order_acquire);
	uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);

	// capacity is constant so we don't need any atomic
	return tail - head == (uint32_t)ring->capacity;
}

static void ring_commit_write(struct sz_ring *ring)
{
	// this release will sync with load on tail
	uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
	atomic_store_explicit(&ring->tail, tail + 1, memory_order_release);
}

static void ring_commit_read(struct sz_ring *ring)
{
	uint32_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);
	atomic_store_explicit(&ring->head, head + 1, memory_order_release);
}

// producer will use this to write, consumer will not use this
// so we can get away with relaxed
static uint32_t write_index(const struct sz_ring *ring)
{
	uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
	return ring->mask & tail;
}

static uint32_t read_index(const struct sz_ring *ring)
{
	uint32_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);
	return ring->mask & head;
}

struct User {
	const char *name;
	uint8_t age;
};

void *producer(void *arg)
{
	struct sz_ring *ring = (struct sz_ring *)arg;
	return NULL;
}

struct sz_arena {
	void *ptr;
	size_t capacity;
	uintptr_t bump;
};

static struct sz_arena *arena_init(size_t capacity, bool huge_pages, bool pre_fault)
{
	if (!is_power_of_two(capacity)) {
		fprintf(stderr, "requested size must needs to be power of two");
		return NULL;
	}

	// for huge pages: sudo sysctl -w vm.nr_hugepages=64
	void *ptr = mmap(NULL, capacity, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS | (huge_pages ? MAP_HUGETLB | MAP_HUGE_2MB : 0), -1, 0);

	if (ptr == MAP_FAILED) {
		perror("mmap");
		return NULL;
	}

	if (pre_fault) {
		// (pre-fault) so we don't get page fault lantency on first access
		madvise(ptr, capacity, MADV_POPULATE_WRITE);
	}

	struct sz_arena *arena = malloc(sizeof(struct sz_arena));
	if (arena == NULL) {
		perror("malloc");
		munmap(ptr, capacity);
		return NULL;
	}

	arena->ptr = ptr;
	arena->capacity = capacity;
	arena->bump = 0;

	return arena;
}

static uint8_t *arena_alloc(struct sz_arena *arena, size_t size, size_t align)
{
	size_t aligned = ALIGN_UP(arena->bump, align);
	if (aligned + size > arena->capacity)
		return NULL;
	arena->bump = aligned + size;
	return (uint8_t *)arena->ptr + aligned;
}

// reuse arena without reallocation
static void arena_reset(struct sz_arena *arena)
{
	arena->bump = 0;
}

static void arena_destroy(struct sz_arena *arena)
{
	munmap(arena->ptr, arena->capacity);
	free(arena);
}

struct sz_free_list {
	struct sz_free_list *next;
};

void free_list_push(struct sz_free_list *new, struct sz_free_list *head)
{
	// head is a (sentinel) node not a pointer to first node
	// [head] -> [x] -> [y] -> null
	// [head] -> [new] -> [x] -> [y] -> [null]
	// head.next is [x]
	// new.next is null initially
	// we want [new] -> [x]
	// and [head] -> [new]
	new->next = head->next;
	head->next = new;
}

static struct sz_free_list *free_list_pop(struct sz_free_list *head)
{
	if (head->next == NULL)
		return NULL;

	struct sz_free_list *node = head->next;
	head->next = head->next->next;
	node->next = NULL;
	return node;
}

static bool free_list_is_empty(const struct sz_free_list *head)
{
	return head->next == NULL;
}

struct list_head {
	struct list_head *prev;
	struct list_head *next;
};

void list_init(struct list_head *head)
{
	head->prev = head;
	head->next = head;
}

static inline void __list_add(struct list_head *new, struct list_head *prev, struct list_head *next)
{
	// i want to insert new b/w prev and next
	// remember one thing: A.next = B <==> B.prev = A
	// read it as A.next = B iff B.prev = A if P then Q and if Q then P
	// initially i have:
	// [prev] <------> [next]
	// i want:
	// [prev] <-----> [new] <----> [next]
	// so new.prev = prev, new.next = next
	//    prev.next = new, prev.prev = next (because its circular already
	//    maintained so ignore)
	//  next.prev = new, next.next = prev

	new->prev = prev;
	new->next = next;
	prev->next = new;
	next->prev = new;
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

int main()
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

	return 0;
}
