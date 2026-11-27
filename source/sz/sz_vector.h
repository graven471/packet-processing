#pragma once

/*
 * so what is a dynamic array ?
 * dynamic array is something that can grow at runtime assume at start array has n elements
 * but during runtime we need to expand it we want n + y elements we can't do that with
 * C arrays it's sized needs to be known at compile time
 * so, what is the solution
 * do runtime allocation if needed we allocate memory of k bytes, where k is the capacity
 * we store n elements
 * if n > k then we need another allocation and probably copying to new location
 * if n < k then we don't need any new copy
 * a dynamic array needs these things:
 * len       -> how many elements an array have
 * capacity  -> how much elements can an array store without doing allocation
 * elem      -> pointer that stores the starting address of the allocation
 * elem_size -> how big is each elements
 *
 * there is two option to initialize the dynamic array
 * 1. init(elem_size) -> just creates new dynamic array stores cap. and elem_size no allocation
 * during push we internally pick capacity and do allocation
 * 2. init_with_capacity(capacity, elem_size) -> allocates memory for n bytes where n is capacity
 * push does not needs new allocation as long as there is space to store stuffs
 *
 * allocation_size = capacity * elem_size
 * address(i) = base + i * elem_size
 *
 * where we will store it?
 * well take a big blob of memory
 * |-------------------------------------|
 * |            4096-byte                |
 * |-------------------------------------|
 * ^                                     ^
 *  0x1000                               0x2000
 * we have [0x1000, 0x2000)
 * e.g.
 * let's say storage is empty initially then user calls push(20), elem_size = 4, len = 0,
 * capacity = 4096
 * we will write elem[len] = 20 and do len++
 * generalizing this, f(i, x) = elem[i] = x, where i is the index and x is the data
 *
 * but, what is the capacity is full i.e. len == capacity in that case we can't push
 * so we have to realloc where new_capacity = capacity * 2 copy all the elements into new location
 * and then push back
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/mman.h>

typedef struct {
    size_t elem_size;
    size_t len;
    size_t capacity; /* capacity in elements not bytes */
    void *data;
} sz_vector;

typedef enum {
    SZ_VECTOR_INVALID_INPUT,
    SZ_VECTOR_ALLOCATION_FAILED,
    SZ_VECTOR_INVALID_CAPACITY,
    SZ_VECTOR_INVALID_PTR,
    SZ_VECTOR_OK = 0,
} sz_vector_result;

static inline sz_vector_result sz_vector_init(size_t elem_size, sz_vector *out) {
    if (elem_size == 0)
        return SZ_VECTOR_INVALID_INPUT;

    out->capacity = 0;
    out->len = 0;
    out->data = nullptr;
    out->elem_size = elem_size;

    return SZ_VECTOR_OK;
}

static inline sz_vector_result sz_vector_init_with_capacity(size_t elem_size,
                                                            size_t initial_capacity,
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

static __always_inline size_t sz_vector_capacity(const sz_vector *self) {
    return self->capacity;
}

static __always_inline size_t sz_vector_len(const sz_vector *self) {
    return self->len;
}

static __always_inline size_t sz_vector_elem_size(const sz_vector *self) {
    return self->elem_size;
}

static inline bool sz_vector_push_raw(sz_vector *self, void *elem) {
    assert(self != nullptr);

    if (self->capacity == 0) {
        size_t elem_size = self->elem_size;
        if (sz_vector_init_with_capacity(elem_size, 8, self) != SZ_VECTOR_OK)
            return false;
    }

    assert(self->data != nullptr);

    // reallocation needed ?
    if (self->len == self->capacity) {
        // we are use 2x growth capacity
        size_t new_capacity = self->capacity * 2; /* (todo): handle overflow */
        size_t new_len = new_capacity * self->elem_size;
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

static inline void *sz_vector_get(const sz_vector *self, const size_t i) {
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
