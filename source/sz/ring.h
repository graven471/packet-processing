#pragma once

#include <stddef.h>
#include <stdint.h>

#include "sz/sz_types.h"

// we want head and tail in different cache lines so producer and consumer
// can modify head and tail without affecting each other cache line
struct sz_ring {
    alignas(64) _Atomic u32 head;
    alignas(64) _Atomic u32 tail;
    usize capacity; /* power of two */
    u32 mask;       /* capacity - 1 */
    void *data;
};

[[nodiscard, gnu::cold]] struct sz_ring *ring_init(usize capacity);

[[nodiscard, gnu::always_inline]] bool is_empty(const struct sz_ring *ring) [[clang::nonblocking]];
[[nodiscard, gnu::always_inline]] bool is_full(const struct sz_ring *_Nonnull ring)
    [[clang::nonblocking]];

[[gnu::always_inline]] void ring_commit_write(struct sz_ring *ring) [[clang::nonblocking]];
[[gnu::always_inline]] void ring_commit_read(struct sz_ring *ring) [[clang::nonblocking]];

[[gnu::always_inline, nodiscard]] u32 write_index(const struct sz_ring *ring)
    [[clang::nonblocking]];
[[gnu::always_inline, nodiscard]] u32 read_index(const struct sz_ring *ring) [[clang::nonblocking]];

[[gnu::cold]] void ring_free(struct sz_ring *ring);
