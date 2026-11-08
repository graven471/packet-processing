#include "arena.h"
#include "sz/common.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

[[gnu::cold]] [[nodiscard]] struct sz_arena *arena_init(size_t capacity, bool huge_pages, bool pre_fault)
{
	if (!is_power_of_two(capacity)) {
		fprintf(stderr, "requested size must needs to be power of two");
		return nullptr;
	}

	// for huge pages: sudo sysctl -w vm.nr_hugepages=64
	void *ptr = mmap(NULL, capacity, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS | (huge_pages ? MAP_HUGETLB | MAP_HUGE_2MB : 0), -1, 0);

	if (ptr == MAP_FAILED) [[clang::unlikely]] {
		perror("mmap");
		return nullptr;
	}

	if (pre_fault) {
		// (pre-fault) so we don't get page fault lantency on first access
		madvise(ptr, capacity, MADV_POPULATE_WRITE);
	}

	struct sz_arena *arena = malloc(sizeof(struct sz_arena));
	if (arena == NULL) [[clang::unlikely]] {
		perror("malloc");
		munmap(ptr, capacity);
		return nullptr;
	}

	arena->ptr = ptr;
	arena->capacity = capacity;
	arena->bump = 0;

	return arena;
}

[[gnu::hot, nodiscard]] uint8_t *arena_alloc(struct sz_arena *_Nonnull arena, size_t size, size_t align)
	[[clang::nonallocating]]
{
	size_t aligned = ALIGN_UP(arena->bump, align);
	if (aligned + size > arena->capacity)
		return nullptr;
	arena->bump = aligned + size;
	return (uint8_t *)arena->ptr + aligned;
}

[[gnu::always_inline]] void arena_reset(struct sz_arena *arena)
{
	arena->bump = 0;
}

[[gnu::cold]] void arena_destroy(struct sz_arena *_Nonnull arena)
{
	munmap(arena->ptr, arena->capacity);
	free(arena);
}
