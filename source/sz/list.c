#include "list.h"

[[gnu::cold]] void SZ_INIT_LIST_HEAD(struct list_head *list)
{
	list->next = list;
	list->prev = list;
}

[[gnu::always_inline, gnu::pure]] bool list_empty(const struct list_head *_Nonnull head)
{
	return head->next == head;
}

[[gnu::always_inline]] void __list_add(struct list_head *new, struct list_head *prev, struct list_head *next)
{
	// A.next = B <==> B.prev = A
	// we have:
	// [prev] <------> [next]
	// we want:
	// [prev] <-----> [new] <----> [next]
	// so new.prev = prev, new.next = next
	//    prev.next = new, prev.prev = next (because its circular already
	//    maintained so ignore)
	//  next.prev = new, next.next = prev

	new->prev = prev;
	new->next = next;
	prev->next = new;
	next->prev = new;
}

[[gnu::always_inline]] void list_add(struct list_head *new, struct list_head *_Nonnull head)
{
	// initially:
	// head <--> [x] <--> [y] and y.next -> head, head.prev -> y
	// we want:
	// head <--> [new] <--> [x] <--> [y] and y.next -> head, head.prev -> y
	// new.prev  = head <==> new.next = head.next
	// head.next = new  <==> x.prev   = new
	__list_add(new, head, head->next);
}

[[gnu::always_inline]] void list_add_tail(struct list_head *new, struct list_head *_Nonnull head)
{
	// initially:
	// head <--> [x] <--> [y] and y.next -> head, head.prev -> y
	// we want:
	// head <--> [x] <--> [y] <--> [new] and new.next -> head, head.prev -> next
	// so,
	// new.next  =  head <==> new.prev         = head.prev
	// head.prev = new   <==> head.prev.next   = new
	__list_add(new, head->prev, head);
}

[[gnu::always_inline]] void list_add_del(struct list_head *_Nonnull entry)
{
	entry->prev->next = entry->next;
	entry->next->prev = entry->prev;
}
