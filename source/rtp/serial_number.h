/*
 * implementation of RFC 1982: Serial Number Arithmetic
 */

#pragma once

#include <stdint.h>

#include <asm-generic/errno-base.h>
#include <sys/cdefs.h>

typedef uint16_t seq_num;

constexpr uint16_t SERIAL_BITS = 16;

static __always_inline int seq_add(seq_num s, uint16_t n, seq_num *out) {
    // outside of range [0 .. (2^(SERIAL_BITS - 1) - 1)] is undefined.
    // in our case its [0 .. (2^(16 - 1) - 1)] = [0 .. (2^(15) - 1)]
    if (n > (1u << (SERIAL_BITS - 1)) - 1)
        return -EINVAL;
    // s' = (s + n) modulo (2 ^ SERIAL_BITS)
    *out = (uint16_t)(s + n);
    return 0;
}

static __always_inline bool seq_lt(seq_num s1, seq_num s2) {
    /*
     s1 is said to less than s2 if, and only if, s1 is not equal to s2, and
     (i1 < i2 and i2 - i1 < 2^(SERIAL_BITS - 1)) or
     (i1 > i2 and i1 - i2 > 2^(SERIAL_BITS - 1))
     */
    if (s1 == s2)
        return false;

    return (s1 < s2 && s2 - s1 < (1u << (SERIAL_BITS - 1)))
           || (s1 > s2 && s1 - s2 > (1u << (SERIAL_BITS - 1)));
}

static __always_inline bool seq_lte(seq_num s1, seq_num s2) {
    // rfc does not defines this function
    return seq_lt(s1, s2) || s1 == s2;
}

static __always_inline bool seq_gt(seq_num s1, seq_num s2) {
    /*
     s1 is said to be greater than s2 if, and only if, s1 is not equal to s2, and
     (i1 < i2 and i2 - i1 > 2^(SERIAL_BITS - 1)) or
     (i1 > i2 and i1 - i2 < 2^(SERIAL_BITS - 1))
     */
    if (s1 == s2)
        return false;

    return (s1 < s2 && s2 - s1 > (1u << (SERIAL_BITS - 1)))
           || (s1 > s2 && s1 - s2 < (1u << (SERIAL_BITS - 1)));
}

static __always_inline bool seq_gte(seq_num s1, seq_num s2) {
    // rfc does not defines this function
    return seq_gt(s1, s2) || s1 == s2;
}
