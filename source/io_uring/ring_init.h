#pragma once

#include <linux/io_uring.h>
#include <stdint.h>

struct sz_uring_context {
	void *sq_ring;
	void *cq_ring;
	void *sqes;
	void *cqes;

	/* (todo): should i store pointers or values */
	uint32_t *sq_mask;
	uint32_t *sq_array;
	uint32_t *sq_head;
	uint32_t *sq_tail;

	uint32_t *cq_mask;
	uint32_t *cq_head;
	uint32_t *cq_tail;
};

[[nodiscard("can fail"), gnu::cold]] int sz_uring_create(const uint32_t entries,
							 struct io_uring_params *_Nonnull params);

void sz_uring_destroy(struct sz_uring_context *_Nonnull ctx);
