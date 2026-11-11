#include "ring_init.h"
#include <asm/unistd_64.h>
#include <errno.h>
#include <linux/io_uring.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <syscall.h>
#include <unistd.h>

/*
    * so here is the core idea of io_uring
    * there is two ring buffers SQ (submission queue) and CQ (completion queue)
    *
    * we define the submission queue entry (sqe) and update the tail of the SQ (since we are producer)
    * kernel fetches sqe from SQ ring and validates it and then updates head of SQ (since kernel is the consumer)
    *
    * kernel does the work and write the result into CQ ring and updates the tail (since kernel is the producer)
    * we then fetch the cqe (completion queue entry) check error i.e. cqe->res < 0 then update head of CQ
    *  (since we are the consumer)
    *
    * this is fully async and there is two operation point sort of
    * 1. Submission point
    * 2. Completion point
    *
    * and submission != completion we don't have to wait for result after submit seq we can do another stuffs
    * and we can check later if kernel finished to job or not
    *
    * we can also do batch uploads if we want
    *
    * we can identify response through `user_data` field
    *
    * sync:
    * for SQ we own the tail and kernel owns the head and kernel checks tail conceptually
    * so, we whenever we update sq->tail we have to do `release` ordering so kernel can do `acquire`
    * and whenever we read head we have to do `acquire` so we sync with kernel `release`
    *
    * for CQ we own the head kernel owns the tail we check for head conceptually
    * when updating cq->head we will do `release` so kernel can do `acquire`
    * if we ever want to read cq->tail then we have to do `acquire`
    */

int sz_uring_create(const uint32_t entries, struct io_uring_params *_Nonnull params)
{
	int retval;
	int ring_fd;
	struct sz_uring_context *context;

	retval = (int)syscall(__NR_io_uring_setup, entries, params);

	if (retval < 0)
		return -errno;

	ring_fd = retval;

	// size of the SQ and CQ ring
	size_t sq_ring_sz = params->sq_off.array + params->sq_entries * sizeof(unsigned);
	size_t cq_ring_sz = params->cq_off.cqes + params->cq_entries * sizeof(struct io_uring_cqe);

	// with this flag set submission queue and completion queue can share a single mmap
	if (params->features & IORING_FEAT_SINGLE_MMAP) {
		if (cq_ring_sz > sq_ring_sz)
			sq_ring_sz = cq_ring_sz;

		cq_ring_sz = sq_ring_sz;
	}

	context = malloc(sizeof(struct sz_uring_context));

	if (context == nullptr) {
		close(ring_fd);
		return -errno;
	}

	context->sq_ring = mmap(nullptr, sq_ring_sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, ring_fd,
				IORING_OFF_SQ_RING);

	if (context->sq_ring == MAP_FAILED) {
		close(ring_fd);
		free(context);
		return -errno;
	}

	if (params->features & IORING_FEAT_SINGLE_MMAP) {
		context->cq_ring = context->sq_ring;
	} else {
		context->cq_ring = mmap(nullptr, cq_ring_sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, ring_fd,
					IORING_OFF_CQ_RING);

		if (context->cq_ring == MAP_FAILED) {
			close(ring_fd);
			munmap(context->sq_ring, sq_ring_sz);
			free(context);
			return -errno;
		}
	}

#define RING_FIELD(ptr, value) (uint32_t *)((char *)ptr) + value;

	context->sq_head = RING_FIELD(context->sq_ring, params->sq_off.head);
	context->sq_tail = RING_FIELD(context->sq_ring, params->sq_off.tail);
	context->sq_mask = RING_FIELD(context->sq_ring, params->cq_off.ring_mask);
	context->sq_array = RING_FIELD(context->sq_ring, params->sq_off.array);

	// submission queue entries array
	context->sqes = mmap(nullptr, params->sq_entries * sizeof(struct io_uring_sqe), PROT_READ | PROT_WRITE,
			     MAP_SHARED | MAP_POPULATE, ring_fd, IORING_OFF_SQES);

	if (context->sqes == MAP_FAILED)
		goto cleanup;

	context->cq_head = RING_FIELD(context->cq_ring, params->cq_off.head);
	context->cq_tail = RING_FIELD(context->cq_ring, params->cq_off.tail);
	context->cq_mask = RING_FIELD(context->cq_ring, params->cq_off.ring_mask);

	// completion queue arrays
	context->cqes = (char *)context->cq_ring + params->cq_off.cqes;

	return 0;

cleanup:
	close(ring_fd);
	munmap(context->sq_ring, sq_ring_sz);
	free(context);
	return -errno;
}

void sz_uring_destroy(struct sz_uring_context *_Nonnull ctx)
{
}
