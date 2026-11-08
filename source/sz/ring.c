#include "ring.h"
#include "sz/common.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

[[nodiscard, gnu::cold]] struct sz_ring *ring_init(size_t capacity)
{
	if (!is_power_of_two(capacity))
		return nullptr;

	struct sz_ring *ring = malloc(sizeof(struct sz_ring));

	if (ring == nullptr) [[clang::unlikely]] {
		perror("malloc");
		return nullptr;
	}

	memset(ring, 0, sizeof(struct sz_ring));

	void *storage = mmap(NULL, capacity, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (storage == MAP_FAILED) {
		perror("mmap");
		free(ring);
		return nullptr;
	}

	ring->capacity = capacity;
	ring->data = storage;
	ring->mask = (uint32_t)capacity - 1;

	return ring;
}

[[nodiscard, gnu::always_inline]] bool is_empty(const struct sz_ring *ring) [[clang::nonblocking]]
{
	// consumer will call this so we need proper sync on tail
	// and for head we can just use relaxed since producer is not gonna use this

	// this sync with producer release on ring->tail
	uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_acquire);
	uint32_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);

	return head == tail;
}

[[nodiscard, gnu::always_inline]] bool is_full(const struct sz_ring *_Nonnull ring) [[clang::nonblocking]]
{
	// producer will use this before writing so we need proper sync on head
	// this sync with consumer write with release
	const uint32_t head = atomic_load_explicit(&ring->head, memory_order_acquire);
	const uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);

	// capacity is constant so we don't need any atomic
	return tail - head == (uint32_t)ring->capacity;
}

[[gnu::always_inline]] void ring_commit_write(struct sz_ring *ring) [[clang::nonblocking]]
{
	// this release will sync with load on tail
	const uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
	atomic_store_explicit(&ring->tail, tail + 1, memory_order_release);
}

[[gnu::always_inline]] void ring_commit_read(struct sz_ring *ring) [[clang::nonblocking]]
{
	const uint32_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);
	atomic_store_explicit(&ring->head, head + 1, memory_order_release);
}

[[gnu::always_inline, nodiscard]] uint32_t write_index(const struct sz_ring *ring) [[clang::nonblocking]]
{
	const uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
	return ring->mask & tail;
}

[[gnu::always_inline, nodiscard]] uint32_t read_index(const struct sz_ring *ring) [[clang::nonblocking]]
{
	const uint32_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);
	return ring->mask & head;
}

[[gnu::cold]] void ring_free(struct sz_ring *ring)
{
	munmap(ring->data, ring->capacity);
	free(ring);
}
