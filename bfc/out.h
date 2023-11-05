#pragma once
#include "../common/types.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Dynamic array of bytes that allows for layout customization.
// Follows ZII (Zero Is Initialization) pattern.
// Deallocation is done through `out_destroy()`.
struct out {
    void *bytes;
    uint32_t len;
    uint32_t cap;
};

void *out_resv(struct out *out, u32 count);

static inline void out_destroy(struct out *out) {
    if (out->cap != 0)
        free(out->bytes);
}

static inline void out_write(struct out *out, void const *src, u32 count) {
    memcpy(out_resv(out, count), src, count);
}

static inline u32 out_write_index(struct out *out, void const *src, u32 count) {
    u32 index = out->len;
    out_write(out, src, count);
    return index;
}

static inline u32 out_resv_index(struct out *out, u32 count) {
    u32 index = out->len;
    out_resv(out, count);
    return index;
}

// vim:ft=c
