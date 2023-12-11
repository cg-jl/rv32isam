#pragma once
#include "../common/types.h"
#include "insn.h"

static i32 bit_cast_i32(u32 v) { return *(i32 *)&v; }

static i32 sext32_imm32(u32 x) { return bit_cast_i32(x); }

static i32 sext32_generic(u32 v) {
    if (v == 0)
        return 0;
    u32 fill_count = __builtin_clz(v);
    u32 bit_pos = 32 - fill_count;
    u32 mask = (1 << (fill_count - 1)) << (bit_pos - 1);
    return v | mask;
}

static i32 sext32_imm12(u16 x_12) {
    u32 x = x_12;
    // find the 12th bit and extend it to 0 or all 1s
    u32 mask = ~((x >> 11) - 1);
    // only interested in the first (32 - 12 = 20) bits
    mask &= 0xfffff000;
    return bit_cast_i32(x_12 | mask);
}

static u32 read_upper_immediate(u32 raw) {
    return sext32_imm32(raw & 0xfffff000);
}

static u32 recover_jal_bits(struct j_format j) {
    return (u32)j.imm_11 << 11 | (u32)j.imm_20 << 20 | (u32)j.imm_10_1 << 1 |
           (u32)j.imm_19_12 << 12;
}
