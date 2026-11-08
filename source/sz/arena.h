#pragma once

#include <stddef.h>
#include <stdint.h>

struct sz_arena {
	void *ptr;
	size_t capacity;
	uintptr_t bump;
};

[[gnu::cold]] [[nodiscard]] struct sz_arena *arena_init(size_t capacity, bool huge_pages, bool pre_fault);

[[gnu::hot, nodiscard]] uint8_t *arena_alloc(struct sz_arena *_Nonnull arena, size_t size, size_t align)
	[[clang::nonallocating]];

[[gnu::always_inline]] void arena_reset(struct sz_arena *arena);

[[gnu::cold]] void arena_destroy(struct sz_arena *_Nonnull arena);
