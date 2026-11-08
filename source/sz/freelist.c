#include "freelist.h"

[[gnu::always_inline, gnu::hot]] void free_list_push(struct sz_free_list *new, struct sz_free_list *head)
	[[clang::nonallocating]]
{
	// head is a (sentinel) node not a pointer to first node
	// [head] -> [x] -> [y] -> null
	// [head] -> [new] -> [x] -> [y] -> [null]
	// head.next is [x]
	// new.next is null initially
	// we want [new] -> [x]
	// and [head] -> [new]
	new->next = head->next;
	head->next = new;
}

[[gnu::always_inline, gnu::hot]] struct sz_free_list *free_list_pop(struct sz_free_list *head) [[clang::nonallocating]]
{
	if (head->next == nullptr)
		return nullptr;

	struct sz_free_list *node = head->next;
	head->next = head->next->next;
	node->next = nullptr;
	return node;
}

[[gnu::always_inline, gnu::pure]] bool free_list_is_empty(const struct sz_free_list *head)
{
	return head->next == nullptr;
}
