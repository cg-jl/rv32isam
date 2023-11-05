#include "out.h"
#include "../common/bit_math.h"

void *out_resv(struct out *out, u32 count) {
    u32 required_cap = out->len + count;
    if (required_cap > out->cap) {
        out->cap = max_po2(next_po2(required_cap), 8);
        out->bytes = realloc(out->bytes, out->cap);
    }
    void *addr = out->bytes + out->len;
    out->len += count;
    return addr;
}
