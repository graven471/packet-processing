#pragma once

struct sz_free_list {
	struct sz_free_list *next;
};

[[gnu::always_inline, gnu::hot]] void free_list_push(struct sz_free_list *new, struct sz_free_list *head)
	[[clang::nonallocating]];

[[gnu::always_inline, gnu::hot]] struct sz_free_list *free_list_pop(struct sz_free_list *head) [[clang::nonallocating]];

[[gnu::always_inline, gnu::pure]] bool free_list_is_empty(const struct sz_free_list *head);
