#define _GNU_SOURCE

#include <assert.h>
#include <iso646.h>
#include <pthread.h>
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

#define ALIGN_UP(x, align) (((x) + (align) - 1) & ~((uintptr_t)(align) - 1))

__attribute__((always_inline)) bool is_power_of_two(int n)
{
	return (n > 0) && ((n & (n - 1)) == 0);
}

// we want head and tail in different cache lines so producer and consumer
// can modify head and tail without affecting each other cache line
struct sz_ring {
	_Alignas(64) _Atomic uint32_t head;
	_Alignas(64) _Atomic uint32_t tail;
	size_t capacity;
	uint32_t mask;
	void *data;
};

static __always_inline struct sz_ring *ring_init(size_t capacity)
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

static inline bool is_empty(const struct sz_ring *ring)
{
	// consumer will call this so we need proper sync on tail
	// and for head we can just use relaxed since producer is not gonna use this

	// this sync with producer release on ring->tail
	uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_acquire);
	uint32_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);

	return head == tail;
}

static inline bool is_full(const struct sz_ring *ring)
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

static inline void ring_commit_read(struct sz_ring *ring)
{
	uint32_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);
	atomic_store_explicit(&ring->head, head + 1, memory_order_release);
}

// producer will use this to write, consumer will not use this
// so we can get away with relaxed
static inline uint32_t write_index(const struct sz_ring *ring)
{
	uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
	return ring->mask & tail;
}

static inline uint32_t read_index(const struct sz_ring *ring)
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

static inline struct sz_arena *arena_init(size_t capacity, bool huge_pages, bool pre_fault)
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

static inline bool free_list_is_empty(const struct sz_free_list *head)
{
	return head->next == NULL;
}

struct list_head {
	struct list_head *prev;
	struct list_head *next;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

// assume we have a struct called process its at vm address 0x1000
// its layout is:
// 0x1000 = name
// 0x1008 = pid
// 0x100C = padding
// 0x1010 = node
// assume offsetof(type, member) = 16
// assume (char *)(ptr) = 0x1010
// then we do, (char*)0x1010 - 16 = 0x1000
// container address = member address - member offset
#define typeof_member(T, m) typeof(((T *)0)->m)

#define __same_type(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))

#define container_of(ptr, type, member)                                                                      \
	({                                                                                                   \
		static_assert(__same_type(*(ptr), typeof_member(type, member)) || __same_type(*(ptr), void), \
			      "pointer type mismatch in container_of()");                                    \
		(type *)((void *)(ptr) - offsetof(type, member));                                            \
	})

#define list_entry(ptr, type, member) container_of(ptr, type, member)

#define list_for_each(pos, head) for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_entry(pos, head, member)                                             \
	for (pos = list_entry((head)->next, typeof(*pos), member); &pos->member != (head); \
	     pos = list_entry(pos->member.next, typeof(*pos), member))

#define list_for_each_entry_reverse(pos, head, member)                                     \
	for (pos = list_entry((head)->prev, typeof(*pos), member); &pos->member != (head); \
	     pos = list_entry(pos->member.prev, typeof(*pos), member))

static inline void INIT_LIST_HEAD(struct list_head *list)
{
	list->next = list;
	list->prev = list;
}

static __always_inline bool list_empty(const struct list_head *head)
{
	return head->next == head;
}

static __always_inline void __list_add(struct list_head *new, struct list_head *prev, struct list_head *next)
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

static __always_inline void list_add(struct list_head *new, struct list_head *head)
{
	// initially:
	// head <--> [x] <--> [y] and y.next -> head, head.prev -> y
	// we want:
	// head <--> [new] <--> [x] <--> [y] and y.next -> head, head.prev -> y
	// new.prev  = head <==> new.next = head.next
	// head.next = new  <==> x.prev   = new
	__list_add(new, head, head->next);
}

static __always_inline void list_add_tail(struct list_head *new, struct list_head *head)
{
	// initially:
	// head <--> [x] <--> [y] and y.next -> head, head.prev -> y
	// we want:
	// head <--> [x] <--> [y] <--> [new] and new.next -> head, head.prev -> next
	// so,
	// new.next  =  head <==> new.prev         = head.prev
	// head.prev = new   <==> head.prev.next   = new
	__list_add(new, head->prev, head);
}

static __always_inline void list_add_del(struct list_head *entry)
{
	entry->prev->next = entry->next;
	entry->next->prev = entry->prev;
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

#define NEXT_POW2(x) (1UL << (64 - __builtin_clzl((x) - 1)))
#define PAGE_SIZE 4096

typedef enum { Free, Partial, Full } slab_state_t;

struct slab {
	uint32_t slots;
	uint32_t free_count;
	slab_state_t state;
	struct sz_arena *arena;
	struct sz_free_list free_list;
	struct list_head node;
};

// (todo): maybe change the name ? or i can use kernel name :)
struct kmem_cache {
	struct list_head slabs;
	uint32_t object_size;
	const char *name;
};

static inline struct kmem_cache *kmem_cache_create(const char *name, uint32_t object_size)
{
	object_size = NEXT_POW2(object_size);
	printf("obj size: %d\n", object_size);

	struct sz_arena *arena = arena_init(PAGE_SIZE, false, true);
	if (!arena)
		return NULL;

	// we want initial n bytes of page to store slab metadata so skip it and align
	uintptr_t offset = ((uintptr_t)arena->ptr) + sizeof(struct slab);
	uintptr_t start = ALIGN_UP(offset, object_size);

	printf("start = %lxlu\n", start);

	uint32_t slots = ((uintptr_t)arena->ptr + PAGE_SIZE - start) >> __builtin_ctz(object_size);
	assert(slots > 0 && "slots can not be zero");

	printf("base address = %lxlu\n", (uintptr_t)arena->ptr);
	printf("slots = %d\n", slots);

	struct slab *slab = arena->ptr;
	// since arena_init allocates `arena` on heap we can store it inside slab
	slab->arena = arena;
	slab->slots = slots;
	slab->free_count = slots;
	slab->state = Free;

	// we want FIFO order not LIFO
	for (uint32_t i = slots; i > 0; --i) {
		uintptr_t addr = (uintptr_t)start + (i - 1) * object_size;
		free_list_push((struct sz_free_list *)addr, &slab->free_list);
	}

	// debugging
	// struct sz_free_list *curr;
	// for (curr = slab->free_list.next; curr != NULL; curr = curr->next) {
	// 	printf("free_list %lxp\n", (uintptr_t)curr);
	// }

	struct kmem_cache *cache = malloc(sizeof(struct kmem_cache));
	if (!cache)
		return NULL;

	cache->name = name;
	cache->object_size = object_size;
	INIT_LIST_HEAD(&cache->slabs);

	// build the initial link
	list_add(&slab->node, &cache->slabs);

	return cache;
}

static inline void *kmem_cache_alloc(struct kmem_cache *cache)
{
	struct slab *slab;
	// todo: refill slab and do state managment
	list_for_each_entry (slab, &cache->slabs, node) {
		printf("%lxlu, slots = %d, free_count = %d\n", (uintptr_t)&slab->free_list, slab->slots,
		       slab->free_count);

		if (slab->state != Free)
			continue;

		struct sz_free_list *block = free_list_pop(&slab->free_list);
		slab->free_count -= 1;
		printf("%lxlu, slots = %d, free_count = %d\n", (uintptr_t)&slab->free_list, slab->slots,
		       slab->free_count);
		return (void *)block;
	}
	return NULL;
}

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

	// test
	LIST_HEAD(process_head);

	struct process a = { .pid = 10, .name = "xwayland" };
	struct process b = { .pid = 20, .name = "swaybg" };

	list_add_tail(&a.node, &process_head);
	list_add(&b.node, &process_head);

	struct process *pos;

	// list_for_each (pos, &process_head) {
	// 	// ptr = node, type = process, member = node
	// 	// ((type *)((char *)(ptr) - offsetof(type, member)))
	// 	// auto x = (char*)(node) - offsetof(process, node)
	// 	// auto x = (char*)(node VA) - P
	// 	// process* = (type*) x
	// 	struct process *p = list_entry(pos, struct process, node);
	// 	printf("id = %d, name = %s\n", p->pid, p->name);
	// }

	list_for_each_entry (pos, &process_head, node) {
		printf("PID: %d, Name: %s\n", pos->pid, pos->name);
	}

	struct kmem_cache *cache = kmem_cache_create("itch_order", sizeof(itch_add_order_t));
	if (!cache) {
		fprintf(stderr, "failed to create cache");
		return -1;
	}

	itch_add_order_t *order = kmem_cache_alloc(cache);

	if (!order) {
		fprintf(stderr, "kmem alloc failed");
		return -1;
	}

	printf("addr = %lxlu\n", (uintptr_t)order);

	*order = (itch_add_order_t){
		.length = 20, .msg_type = 10, .side = 'B', .shares = 30, .symbol = 0, .timestamp = 123424
	};

	printf("%d, %d, %c \n", order->length, order->msg_type, order->side);

	return 0;
}
