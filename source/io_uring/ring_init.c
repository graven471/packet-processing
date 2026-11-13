#include "ring_init.h"
#include "io_uring/syscall.h"
#include <asm-generic/errno-base.h>
#include <asm/unistd_64.h>
#include <errno.h>
#include <linux/io_uring.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

struct sz_uring_context *sz_uring_create(const uint32_t entries, struct io_uring_params *_Nonnull params)
{
	if (entries == 0) {
		errno = EINVAL;
		return nullptr;
	}

	int retval;
	unsigned int ring_fd;
	struct sz_uring_context *context;

	retval = (int)syscall(__NR_io_uring_setup, entries, params);

	if (retval < 0)
		return nullptr;

	ring_fd = (unsigned)retval;

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
		return nullptr;
	}

	memset(context, 0, sizeof(struct sz_uring_context));

	context->fd = ring_fd;
	context->features = params->features;
	context->sq_entries = entries;

	context->sq_ring = mmap(nullptr, sq_ring_sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, ring_fd,
				IORING_OFF_SQ_RING);

	if (context->sq_ring == MAP_FAILED) {
		close(ring_fd);
		free(context);
		return nullptr;
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
			return nullptr;
		}
	}

#define RING_FIELD(ptr, value) (uint32_t *)((char *)ptr) + value;

	context->sq_head = RING_FIELD(context->sq_ring, params->sq_off.head);
	context->sq_tail = RING_FIELD(context->sq_ring, params->sq_off.tail);
	context->sq_mask = RING_FIELD(context->sq_ring, params->cq_off.ring_mask);
	context->sq_array = RING_FIELD(context->sq_ring, params->sq_off.array);
	context->sq_flags = RING_FIELD(context->sq_ring, params->sq_off.flags);
	context->sq_size = sq_ring_sz;
	context->__cached_sq_tail = context->sq_tail;

	// submission queue entries array
	context->sqes = mmap(nullptr, params->sq_entries * sizeof(struct io_uring_sqe), PROT_READ | PROT_WRITE,
			     MAP_SHARED | MAP_POPULATE, ring_fd, IORING_OFF_SQES);

	if (context->sqes == MAP_FAILED)
		goto error;

	context->cq_head = RING_FIELD(context->cq_ring, params->cq_off.head);
	context->cq_tail = RING_FIELD(context->cq_ring, params->cq_off.tail);
	context->cq_mask = RING_FIELD(context->cq_ring, params->cq_off.ring_mask);
	context->cq_size = cq_ring_sz;
	context->__cached_cq_tail = context->cq_tail;

	// completion queue arrays
	context->cqes = (struct io_uring_cqe *)((char *)context->cq_ring) + params->cq_off.cqes;

	return context;

error:
	close(ring_fd);
	munmap(context->sq_ring, sq_ring_sz);
	if (!(params->features & IORING_FEAT_SINGLE_MMAP))
		munmap(context->cq_ring, cq_ring_sz);
	free(context);
	return nullptr;
}

struct io_uring_sqe *sz_uring_get_sqe(struct sz_uring_context *_Nonnull ring)
{
	uint32_t head = io_uring_smp_load_acquire(ring->sq_head);
	uint32_t tail = io_uring_smp_load_relaxed(ring->sq_tail);
	uint32_t pending = *ring->__cached_sq_tail;

	if ((tail + pending - head) >= ring->sq_entries) [[clang::unlikely]]
		return nullptr;

	uint32_t index = (tail + pending) & *ring->sq_mask;

	struct io_uring_sqe *sqe = &ring->sqes[index];
	memset(sqe, 0, sizeof(struct io_uring_sqe));
	ring->__cached_sq_tail++;
	return sqe;
}

inline int sz_uring_submit(struct sz_uring_context *_Nonnull ring)
{
	// (todo): check flags if IORING_SETUP_SQPOLL is set then we need to call wakeup
	// using io_uring_enter syscall

	// since we are the only one who updates the tail we don't need sync
	uint32_t tail = io_uring_smp_load_relaxed(ring->sq_tail);
	uint32_t pending = *ring->__cached_sq_tail;

	if (pending == 0) [[clang::unlikely]]
		return 0;

	// fill the sqe_entry into the sqe ring array
	for (uint32_t i = 0; i < pending; ++i) {
		uint32_t index = (tail + i) & *ring->sq_mask;
		ring->sq_array[index] = index;
		index = (tail + i) & *ring->sq_mask;
	}

	// if this feature is not set then we must need to do io_uring_enter manually
	if (!(ring->features & IORING_SETUP_SQPOLL)) [[clang::unlikely]] {
		// (todo): currently we are not waiting for any request to be completed
		int retval = io_uring_enter(ring->fd, pending, 0, 0, nullptr);

		if (retval < 0) [[clang::unlikely]]
			// this is hard error nothing got submitted so here we can just return error
			return -retval;

		if ((uint32_t)retval < pending) [[clang::unlikely]] {
			uint32_t sub_rem = pending - (uint32_t)retval;
			io_uring_smp_store_release(ring->sq_tail, (tail + pending - sub_rem));
			*ring->__cached_sq_tail = sub_rem;
			return (int)sub_rem;
		}

		// here we know all request has been succeeded
		io_uring_smp_store_release(ring->sq_tail, (tail + pending));
		// reset cache
		ring->__cached_sq_tail = 0;
		return (int)pending;
	}

	if (ring->features & IORING_SETUP_SQPOLL) [[clang::likely]] {
		// wakeup the kernel thread guten morgen i guess
		if (*ring->sq_flags & IORING_SQ_NEED_WAKEUP) {
			// this will not fail i guess
			io_uring_enter(ring->fd, 0, 0, IORING_ENTER_SQ_WAKEUP, nullptr);
		}
	}

	// there is no way for us to know how many request got succeed in IORING_SETUP_SQPOLL kernel
	// picks up asynchronously so yeah for now its inconsistence and i don't know how to fix it
	// btw very few people knows that there is a "t" in inconsistence xiexie
	io_uring_smp_store_release(ring->sq_tail, (tail + pending));
	ring->__cached_sq_tail = 0;
	return (int)pending;
}

void sz_uring_destroy(struct sz_uring_context *_Nonnull ctx)
{
	if (ctx == nullptr)
		return;

	close(ctx->fd);
	munmap(ctx->sq_ring, ctx->sq_size);
	if (!(ctx->features & IORING_FEAT_SINGLE_MMAP))
		munmap(ctx->cq_ring, ctx->cq_size);
	free(ctx);
}
