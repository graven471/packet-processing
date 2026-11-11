#pragma once

#include <assert.h>
#include <immintrin.h>
#include <stdatomic.h>

struct spinlock {
	atomic_bool flag;
};

#define SPINLOCK_INIT { ATOMIC_VAR_INIT(false) }

static inline void spinlock_lock(struct spinlock *lock)
{
	for (;;) {
		if (!atomic_exchange_explicit(&lock->flag, true, memory_order_release))
			return;

		while (atomic_load_explicit(&lock->flag, memory_order_relaxed))
			_mm_pause();
	}
}

static inline void spinlock_unlock(struct spinlock *lock)
{
	atomic_store_explicit(&lock->flag, false, memory_order_release);
}
