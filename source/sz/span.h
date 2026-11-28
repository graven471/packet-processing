#pragma once

#include <stddef.h>
#include <stdint.h>

#include "sz/sz_types.h"

typedef struct {
    const uint8_t *ptr;
    usize len;
} sz_span;

[[clang::always_inline, gnu::const,
  nodiscard]] sz_span static inline sz_span_make(const uint8_t *ptr, usize len) {
    return (sz_span){.ptr = ptr, .len = len};
}

[[clang::always_inline, gnu::const]] bool static inline sz_span_is_empty(sz_span self) {
    return self.len == 0;
}

[[clang::always_inline, gnu::const]] bool static inline sz_span_dangling(sz_span self) {
    return self.ptr == nullptr;
}

[[clang::always_inline, gnu::const]] usize static inline sz_span_size(sz_span self) {
    return self.len;
}

[[clang::always_inline, gnu::const,
  nodiscard]] sz_span static inline sz_span_subspan(sz_span self, usize offset, usize len) {
    if (offset > self.len || len > self.len - offset)
        return sz_span_make(nullptr, 0);
    return (sz_span){.ptr = self.ptr + offset, .len = len};
}

[[clang::always_inline, gnu::const]] sz_span static inline sz_span_first_n(sz_span self, usize n) {
    return sz_span_subspan(self, 0, n);
}

[[clang::always_inline, gnu::const]] sz_span static inline sz_span_last_n(sz_span self, usize n) {
    return n >= self.len ? sz_span_make(nullptr, 0) : sz_span_subspan(self, self.len - n, n);
}

[[clang::always_inline, gnu::const]] sz_span static inline sz_span_drop(sz_span self, usize n) {
    // assume self = [0x1000, 0x1010), so self.len = 16
    // drop(self, 4) should give a subspan of [0x1004, 0x01010)
    return n >= self.len ? sz_span_make(nullptr, 0) : sz_span_subspan(self, n, self.len - n);
}

[[clang::always_inline, gnu::const]] sz_span static inline sz_span_take(sz_span self, usize n) {
    // keep the first n bytes and discard the remainder
    return sz_span_subspan(self, 0, n);
}

[[clang::always_inline, gnu::const]] const static inline uint8_t *sz_span_at(sz_span self,
                                                                             usize index) {
    if (index > self.len) [[clang::unlikely]]
        return nullptr;
    return &self.ptr[index];
}

[[clang::always_inline, gnu::const]] const static inline uint8_t *sz_span_first(sz_span self) {
    if (sz_span_is_empty(self)) [[clang::unlikely]]
        return nullptr;
    return &self.ptr[0];
}

[[clang::always_inline, gnu::const]] const static inline uint8_t *sz_span_last(sz_span self) {
    if (sz_span_is_empty(self)) [[clang::unlikely]]
        return nullptr;
    return &self.ptr[self.len - 1];
}
