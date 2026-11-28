#pragma once

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <asm-generic/errno-base.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "sz/macros.h"
#include "sz/sz_types.h"

enum { ARENA_FLAG_NONE = 0, ARENA_FLAG_HUGE_PAGES = 1 << 1 };

struct sz_arena_config {
    u8 flags;
    usize capacity;
    bool pre_fault;
};

struct sz_arena {
    u8 *storage;
    usize capacity;
    usize offset;
};

static inline int sz_arena_init(const struct sz_arena_config *cfg, struct sz_arena *out) {
    if (cfg == nullptr || out == nullptr || cfg->capacity == 0)
        return -EINVAL;

    void *storage =
        mmap(nullptr,
             cfg->capacity,
             PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | ((cfg->flags & ARENA_FLAG_HUGE_PAGES) ? MAP_HUGETLB : 0),
             -1,
             0);

    if (storage == MAP_FAILED)
        return -errno;

    if (cfg->pre_fault) {
        int retval = madvise(storage, cfg->capacity, MADV_POPULATE_WRITE);
        if (retval != 0)
            return -errno;
    }

    out->storage = (u8 *)storage;
    out->offset = 0;
    out->capacity = cfg->capacity;

    return 0;
}

// todo: sz_arena_set_alignment

static inline u8 *sz_arena_alloc_aligned(struct sz_arena *self, usize n, usize alignment) {
    assert((alignment & (alignment - 1)) == 0);
    if (UNLIKELY(n > self->capacity))
        return nullptr;
    // assume storage = 0x1000 and offset = 8 i.e. we allocated first 8-bytes already
    // then address = 0x1000 + 8 = 0x1008
    const uptr address = (uptr)(self->storage + self->offset);
    // how may bytes to skip to find suitable starting address
    const uptr padding = (-address) & (alignment - 1);
    const uptr aligned_offset = self->offset + padding;
    self->offset = aligned_offset + n;
    return (u8 *)(self->storage + aligned_offset);
}

static __always_inline u8 *sz_arena_alloc(struct sz_arena *self, usize n) {
    return sz_arena_alloc_aligned(self, n, alignof(max_align_t));
}

static __always_inline u8 *sz_arena_alloc_zeroed(struct sz_arena *self, usize n) {
    u8 *addr = sz_arena_alloc(self, n);
    if (addr == nullptr)
        return nullptr;
    memset(addr, 0, n);
    return addr;
}

static __always_inline void sz_arena_reset(struct sz_arena *self) {
    self->offset = 0;
}

static __always_inline usize sz_arena_pos(struct sz_arena *self) {
    return self->offset;
}

static __always_inline void sz_arena_release(struct sz_arena *self) {
    if (self == nullptr || self->storage == nullptr)
        return;

    munmap(self->storage, self->capacity);
    self->offset = 0;
}
