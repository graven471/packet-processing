#pragma once

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include <linux/io_uring.h>

#include "sz/sz_types.h"

#define io_uring_smp_store_release(p, v)                                                           \
    atomic_store_explicit((_Atomic typeof(*(p)) *)(p), (v), memory_order_release)
#define io_uring_smp_load_acquire(p)                                                               \
    atomic_load_explicit((_Atomic typeof(*(p)) *)(p), memory_order_acquire)
#define io_uring_smp_load_relaxed(p)                                                               \
    atomic_load_explicit((_Atomic typeof(*(p)) *)(p), memory_order_relaxed)

struct sz_uring_context {
    unsigned int fd;

    u32 sq_entries;
    u32 flags;

    void *sq_ring;
    void *cq_ring;
    struct io_uring_sqe *sqes;
    struct io_uring_cqe *cqes;

    /* (todo): should i store pointers or values */
    u32 *sq_mask;
    u32 *sq_array;
    u32 *sq_head;
    u32 *sq_tail;
    u32 *sq_flags;
    usize sq_size;

    u32 *cq_mask;
    u32 *cq_head;
    u32 *cq_tail;
    usize cq_size;

    u32 __cached_sq_tail;
    u32 __cached_cq_head;
};

[[nodiscard("check the result before using the context"), gnu::cold]] struct sz_uring_context *
sz_uring_create(const u32 entries, struct io_uring_params *_Nonnull params);

// get a seq returns nullptr if there is no usable sqe
[[gnu::always_inline]] struct io_uring_sqe *sz_uring_get_sqe(struct sz_uring_context *_Nonnull ring)
    [[clang::nonblocking]];
[[gnu::hot]] int sz_uring_submit(struct sz_uring_context *_Nonnull ring);

[[gnu::hot, clang::always_inline]] struct io_uring_cqe *
sz_uring_cq_peek(struct sz_uring_context *_Nonnull ring) [[clang::nonblocking]];

// if cq ring is empty the it waits for atleast one entry before returning
[[gnu::hot, clang::always_inline]] struct io_uring_cqe *
sz_uring_cq_peek_wait(struct sz_uring_context *_Nonnull ring) [[clang::blocking]];

[[gnu::hot, clang::always_inline]] int sz_uring_cq_seen(struct sz_uring_context *_Nonnull ring);

[[gnu::cold]] void sz_uring_destroy(struct sz_uring_context *_Nonnull ctx);
