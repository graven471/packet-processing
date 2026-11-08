#include "slab.h"
#include "sz/arena.h"
#include "sz/common.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

[[gnu::cold, nodiscard]] struct kmem_cache *kmem_cache_create(const char *name, uint32_t object_size)
{
	object_size = NEXT_POW2(object_size);
	printf("obj size: %d\n", object_size);

	struct sz_arena *arena = arena_init(PAGE_SIZE, false, true);
	if (!arena) [[clang::unlikely]]
		return nullptr;

	// we want initial n bytes of page to store slab metadata so skip it and align
	const uintptr_t offset = ((uintptr_t)arena->ptr) + sizeof(struct slab);
	const uintptr_t start = ALIGN_UP(offset, object_size);

	printf("start = %lxlu\n", start);

	const uint64_t slots = ((uintptr_t)arena->ptr + PAGE_SIZE - start) >> __builtin_ctz(object_size);
	assert(slots > 0 && "slots can not be zero");

	printf("base address = %lxlu\n", (uintptr_t)arena->ptr);
	printf("slots = %lu\n", slots);

	struct slab *slab = arena->ptr;
	// since arena_init allocates `arena` on heap we can store it inside slab
	slab->arena = arena;
	slab->slots = (uint32_t)slots;
	slab->free_count = (uint32_t)slots;
	slab->state = Free;

	for (uint64_t i = slots; i > 0; --i) {
		const uintptr_t addr = (uintptr_t)start + (i - 1) * object_size;
		free_list_push((struct sz_free_list *)addr, &slab->free_list);
	}

	struct kmem_cache *cache = malloc(sizeof(struct kmem_cache));
	if (!cache) [[clang::unlikely]]
		return nullptr;

	cache->name = name;
	cache->object_size = object_size;
	SZ_INIT_LIST_HEAD(&cache->slabs);

	// build the initial link
	list_add(&slab->node, &cache->slabs);

	return cache;
}

[[nodiscard, gnu::hot, gnu::always_inline, gnu::returns_nonnull]] void *
kmem_cache_alloc(struct kmem_cache *_Nonnull cache)
{
	struct slab *slab;
	// todo: refill slab and do state managment
	list_for_each_entry (slab, &cache->slabs, node) {
		printf("%lxlu, slots = %d, free_count = %d\n", (uintptr_t)&slab->free_list, slab->slots,
		       slab->free_count);

		if (slab->state != Free) [[clang::unlikely]]
			continue;

		struct sz_free_list *block = free_list_pop(&slab->free_list);
		slab->free_count -= 1;
		printf("%lxlu, slots = %d, free_count = %d\n", (uintptr_t)&slab->free_list, slab->slots,
		       slab->free_count);
		return (void *)block;
	}
	// (todo): this is temp once i add refill logic i will remove this
	return nullptr;
}

// (todo): build this
bool kmem_cache_alloc_bulk(struct kmem_cache *_Nonnull cache, size_t size, void **p)
{
	return false;
}

void kmem_cache_free_bulk(struct kmem_cache *_Nonnull cache, size_t size, void **_Nonnull p)
{
}

void kmem_cache_free(struct kmem_cache *_Nonnull cache, void *_Nonnull obj)
{
	struct slab *slab;
	list_for_each_entry (slab, &cache->slabs, node) {
		free_list_push((struct sz_free_list *)obj, &slab->free_list);
		slab->free_count += 1;
	}
}
