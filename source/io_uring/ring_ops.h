#pragma once

#include "ring_init.h"
#include <linux/io_uring.h>
#include <sys/types.h>

[[clang::always_inline]] struct io_uring_sqe *sz_uring_prep_openat(struct sz_uring_context *_Nonnull ring, int dirfd,
								   const char *path, int flags, mode_t mode)
	[[clang::nonblocking]];

[[clang::always_inline]] struct io_uring_sqe *sz_uring_prep_read(struct sz_uring_context *_Nonnull ring, int fd,
								 uint64_t offset, void *_Nonnull buf, size_t buf_len)
	[[clang::nonblocking]];

[[clang::always_inline]] struct io_uring_sqe *sz_uring_prep_write(struct sz_uring_context *_Nonnull ring, int fd,
								  uint64_t offset, void *_Nonnull buf, size_t buf_len)
	[[clang::nonblocking]];

[[clang::always_inline]] struct io_uring_sqe *sz_uring_prep_close(struct sz_uring_context *_Nonnull ring, int fd)
	[[clang::nonblocking]];
