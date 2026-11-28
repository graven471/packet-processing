#pragma once

/*
 * (todo): add SBO store first n elements on stack then mmap
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <sys/mman.h>

#include "sz/sz_types.h"

typedef struct {
    usize elem_size;
    usize len;
    usize capacity; /* capacity in elements not bytes */
    void *data;
} sz_vector;

typedef enum {
    SZ_VECTOR_INVALID_INPUT,
    SZ_VECTOR_ALLOCATION_FAILED,
    SZ_VECTOR_INVALID_CAPACITY,
    SZ_VECTOR_INVALID_PTR,
    SZ_VECTOR_OK = 0,
} sz_vector_result;

static inline sz_vector_result sz_vector_init(usize elem_size, sz_vector *out) {
    if (elem_size == 0)
        return SZ_VECTOR_INVALID_INPUT;

    out->capacity = 0;
    out->len = 0;
    out->data = nullptr;
    out->elem_size = elem_size;

    return SZ_VECTOR_OK;
}

static inline sz_vector_result sz_vector_init_with_capacity(usize elem_size, usize initial_capacity,
                                                            sz_vector *out) {
    if (elem_size == 0 || initial_capacity == 0)
        return SZ_VECTOR_INVALID_INPUT;

    void *ptr = mmap(nullptr,
                     elem_size * initial_capacity,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS,
                     -1,
                     0);

    if (ptr == MAP_FAILED)
        return SZ_VECTOR_ALLOCATION_FAILED;

    out->capacity = initial_capacity;
    out->len = 0;
    out->elem_size = elem_size;
    out->data = ptr;

    return SZ_VECTOR_OK;
}

static __always_inline bool sz_vector_is_empty(const sz_vector *self) {
    return self->len == 0;
}

static __always_inline usize sz_vector_capacity(const sz_vector *self) {
    return self->capacity;
}

static __always_inline usize sz_vector_len(const sz_vector *self) {
    return self->len;
}

static __always_inline usize sz_vector_elem_size(const sz_vector *self) {
    return self->elem_size;
}

static inline bool sz_vector_push_raw(sz_vector *self, void *elem) {
    assert(self != nullptr);

    if (self->capacity == 0) {
        usize elem_size = self->elem_size;
        if (sz_vector_init_with_capacity(elem_size, 8, self) != SZ_VECTOR_OK)
            return false;
    }

    assert(self->data != nullptr);

    // reallocation needed ?
    if (self->len == self->capacity) {
        // 2x growth capacity
        usize new_capacity = self->capacity * 2; /* (todo): handle overflow */
        usize new_len = new_capacity * self->elem_size;
        void *new_data =
            mremap(self->data, self->capacity * self->elem_size, new_len, MREMAP_MAYMOVE);
        if (new_data == MAP_FAILED)
            return false;
        self->data = new_data;
        self->capacity = new_capacity;
    }

    uint8_t *data = self->data;
    memcpy(data + (self->len * self->elem_size), elem, self->elem_size);
    self->len++;
    return true;
}

static inline void *sz_vector_pop_raw(sz_vector *self) {
    assert(self != nullptr || self->data != nullptr);
    if (sz_vector_is_empty(self))
        return nullptr;
    uint8_t *data = self->data;
    void *elem = data + ((self->len - 1) * self->elem_size);
    assert(elem != nullptr);
    self->len--;
    return elem;
}

static inline void *sz_vector_get(const sz_vector *self, const usize i) {
    assert(self != nullptr || self->data != nullptr);
    if (i >= self->len)
        return nullptr;

    uint8_t *data = self->data;
    void *elem = data + (i * self->elem_size);
    assert(elem != nullptr);
    return elem;
}

static inline void sz_vector_deinit(sz_vector *self) {
    munmap(self->data, self->capacity * self->elem_size);
    memset(self, 0, sizeof(sz_vector));
}

#define sz_vector_push(vec, value) sz_vector_push_raw((vec), (typeof(value)[]){value})
#define sz_vector_pop(vec) sz_vector_pop_raw(vec)
