#pragma once

#if defined(__has_builtin)
#if __has_builtin(__builtin_expect)
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif
#elif defined(__GNUC__)
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif

#ifndef LIKELY
#define LIKELY(x) (!!(x))
#define UNLIKELY(x) (!!(x))
#endif
