#pragma once

#define ALIGN_UP(x, align) (((x) + (align) - 1) & ~((uintptr_t)(align) - 1))
#define NEXT_POW2(x) (1UL << (64 - __builtin_clzl((x) - 1)))

__attribute__((always_inline, const)) inline bool is_power_of_two(int n) [[clang::nonblocking]]
{
	return (n > 0) && ((n & (n - 1)) == 0);
}
