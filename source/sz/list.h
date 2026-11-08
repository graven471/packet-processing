#pragma once

struct list_head {
	struct list_head *prev;
	struct list_head *next;
};

#define SZ_LIST_HEAD_INIT(name) { &(name), &(name) }
#define SZ_LIST_HEAD(name) struct list_head name = SZ_LIST_HEAD_INIT(name)

// assume we have a struct called process its at vm address 0x1000
// its layout is:
// 0x1000 = name
// 0x1008 = pid
// 0x100C = padding
// 0x1010 = node
// assume offsetof(type, member) = 16
// assume (char *)(ptr) = 0x1010
// then we do, (char*)0x1010 - 16 = 0x1000
// container address = member address - member offset
#define typeof_member(T, m) typeof(((T *)0)->m)

#define __same_type(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))

#define container_of(ptr, type, member)                                                                      \
	({                                                                                                   \
		static_assert(__same_type(*(ptr), typeof_member(type, member)) || __same_type(*(ptr), void), \
			      "pointer type mismatch in container_of()");                                    \
		(type *)((void *)(ptr) - offsetof(type, member));                                            \
	})

#define list_entry(ptr, type, member) container_of(ptr, type, member)

#define list_for_each(pos, head) for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_entry(pos, head, member)                                             \
	for (pos = list_entry((head)->next, typeof(*pos), member); &pos->member != (head); \
	     pos = list_entry(pos->member.next, typeof(*pos), member))

#define list_for_each_entry_reverse(pos, head, member)                                     \
	for (pos = list_entry((head)->prev, typeof(*pos), member); &pos->member != (head); \
	     pos = list_entry(pos->member.prev, typeof(*pos), member))

[[gnu::cold]] void SZ_INIT_LIST_HEAD(struct list_head *list);

[[gnu::always_inline, gnu::pure]] bool list_empty(const struct list_head *_Nonnull head);

[[gnu::always_inline]] void __list_add(struct list_head *new, struct list_head *prev, struct list_head *next);

[[gnu::always_inline]] void list_add(struct list_head *new, struct list_head *_Nonnull head);
[[gnu::always_inline]] void list_add_tail(struct list_head *new, struct list_head *_Nonnull head);

[[gnu::always_inline]] void list_add_del(struct list_head *_Nonnull entry);
