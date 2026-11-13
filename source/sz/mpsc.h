#pragma once

#include <assert.h>
#include <stdalign.h>
#include <stdatomic.h>

// [struct mspsc_ring][slot0][slot1]...[slotN]
struct mpsc_ring {
	alignas(64) _Atomic uint32_t head;
	alignas(64) _Atomic uint32_t tail;
	size_t size;
	void *storage;
	uint32_t mask; /* size - 1 */
};
