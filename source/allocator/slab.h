#pragma once

#include "sz/freelist.h"
#include "sz/list.h"
#include <stddef.h>
#include <stdint.h>

constexpr size_t PAGE_SIZE = 4096;

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

[[gnu::cold, nodiscard]] struct kmem_cache *kmem_cache_create(const char *name, uint32_t object_size);

[[nodiscard, gnu::hot, gnu::always_inline, gnu::returns_nonnull]] void *
kmem_cache_alloc(struct kmem_cache *_Nonnull cache);

// (todo): build this
bool kmem_cache_alloc_bulk(struct kmem_cache *_Nonnull cache, size_t size, void **p);
void kmem_cache_free_bulk(struct kmem_cache *_Nonnull cache, size_t size, void **_Nonnull p);

void kmem_cache_free(struct kmem_cache *_Nonnull cache, void *_Nonnull obj);
