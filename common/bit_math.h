#pragma once

#include "types.h"

// Given `align` as a power of two, returns the next value >= `val` that is 0
// modulo `align`.
static inline u32 align_upwards(u32 val, u32 align) {
    return (val + (align - 1)) & ~(align - 1);
}

// Retuns the power of two that is >= `val`.
static inline u32 next_po2(u32 val) {
    // Operation:
    // 2^(⌊log_2(val - 1)⌋ + 1)
    // Uses `val - 1` so that ⌊log_2(val - 1)⌋ is `n - 1` if `val` equals `2^n`.
    // The (& 31) is to ensure that the case for 0 returns 1, since bit shifting
    // more than 31 yields undefined behavior.
    // Note that the case of 0 results in `__builtin_clz(..) == 0`, since
    // subtraction wraps.
    return 1 << ((32 - __builtin_clz(val - 1)) & 31);
}

// Given `a` and `b` powers of two, returns the bigger value.
static inline u32 max_po2(u32 a, u32 b) {
    // OR both values and then retrieve the top most bit.
    a |= b;
    return 1 << (31 - __builtin_clz(a));
}
