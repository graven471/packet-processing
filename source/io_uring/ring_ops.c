#include "ring_ops.h"
#include "io_uring/ring_init.h"
#include <linux/io_uring.h>

struct io_uring_sqe *sz_uring_prep_openat(struct sz_uring_context *ring, int dirfd, const char *path, int flags,
					  mode_t mode)
{
	struct io_uring_sqe *sqe = sz_uring_get_sqe(ring);
	if (!sqe) [[clang::unlikely]]
		return nullptr;

	sqe->opcode = IORING_OP_OPENAT;
	sqe->fd = dirfd;
	sqe->addr = (uint64_t)path;
	sqe->open_flags = flags;
	sqe->len = mode;
	return sqe;
}

struct io_uring_sqe *sz_uring_prep_read(struct sz_uring_context *_Nonnull ring, int fd, uint64_t offset,
					void *_Nonnull buf, size_t buf_len)
{
	struct io_uring_sqe *sqe = sz_uring_get_sqe(ring);
	if (!sqe) [[clang::unlikely]]
		return nullptr;

	sqe->opcode = IORING_OP_READ;
	sqe->addr = (uint64_t)buf;
	sqe->len = buf_len;
	sqe->fd = fd;
	sqe->off = offset;
	return sqe;
}

struct io_uring_sqe *sz_uring_prep_write(struct sz_uring_context *_Nonnull ring, int fd, uint64_t offset,
					 void *_Nonnull buf, size_t buf_len)
{
	struct io_uring_sqe *sqe = sz_uring_get_sqe(ring);
	if (!sqe) [[clang::unlikely]]
		return nullptr;

	sqe->opcode = IORING_OP_WRITE;
	sqe->addr = (uint64_t)buf;
	sqe->len = buf_len;
	sqe->fd = fd;
	sqe->off = offset;
	return sqe;
}

struct io_uring_sqe *sz_uring_prep_close(struct sz_uring_context *_Nonnull ring, int fd)
{
	struct io_uring_sqe *sqe = sz_uring_get_sqe(ring);
	if (!sqe) [[clang::unlikely]]
		return nullptr;

	sqe->opcode = IORING_OP_CLOSE;
	sqe->fd = fd;
	return sqe;
}
