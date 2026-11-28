#pragma once

#include <stddef.h>
#include <stdint.h>
#include <uchar.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef char16_t c16;
typedef int32_t b32;
typedef int32_t i32;
typedef uint32_t u32;
typedef uint64_t u64;
typedef float f32;
typedef double f64;
typedef uintptr_t uptr;
typedef char byte;
typedef ptrdiff_t size;
typedef size_t usize;

#if defined(__clang__)
#define sz_no_discard [[nodiscard]]
#elif defined(__GNUC__)
#define sz_no_discard __attribute__((warn_unused_result))
#elif defined(_MSC_VER)
#define sz_no_discard _Check_return_
#else
#define sz_no_discard
#endif
