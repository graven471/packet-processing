#pragma once

#include <linux/io_uring.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#define io_uring_smp_store_release(p, v) atomic_store_explicit((_Atomic typeof(*(p)) *)(p), (v), memory_order_release)
#define io_uring_smp_load_acquire(p) atomic_load_explicit((_Atomic typeof(*(p)) *)(p), memory_order_acquire)
#define io_uring_smp_load_relaxed(p) atomic_load_explicit((_Atomic typeof(*(p)) *)(p), memory_order_relaxed)

struct sz_uring_context {
	unsigned int fd;

	uint32_t sq_entries;
	uint32_t features;

	void *sq_ring;
	void *cq_ring;
	struct io_uring_sqe *sqes;
	struct io_uring_cqe *cqes;

	/* (todo): should i store pointers or values */
	uint32_t *sq_mask;
	uint32_t *sq_array;
	uint32_t *sq_head;
	uint32_t *sq_tail;
	uint32_t *sq_flags;
	size_t sq_size;

	uint32_t *cq_mask;
	uint32_t *cq_head;
	uint32_t *cq_tail;
	size_t cq_size;

	uint32_t *__cached_sq_tail;
	uint32_t *__cached_cq_tail;
};

[[nodiscard("check the result before using the context"), gnu::cold]] struct sz_uring_context *
sz_uring_create(const uint32_t entries, struct io_uring_params *_Nonnull params);

// get a seq returns nullptr if there is no usable sqe
[[gnu::always_inline]] struct io_uring_sqe *sz_uring_get_sqe(struct sz_uring_context *_Nonnull ring)
	[[clang::nonblocking]];
[[gnu::hot]] inline int sz_uring_submit(struct sz_uring_context *_Nonnull ring) [[clang::nonblocking]];

[[gnu::cold]] void sz_uring_destroy(struct sz_uring_context *_Nonnull ctx);
