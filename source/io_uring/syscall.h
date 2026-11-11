#pragma once
#include <bits/types/sigset_t.h>
#include <liburing.h>
#include <linux/io_uring.h>
#include <syscall.h>
#include <unistd.h>

[[clang::always_inline]] int io_uring_enter(unsigned int fd, unsigned int to_submit, unsigned int min_complete,
					    unsigned int flags, sigset_t *sig)
{
	return (int)syscall(__NR_io_uring_enter, fd, to_submit, min_complete, flags, sig);
}
