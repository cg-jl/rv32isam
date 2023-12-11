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

static u32 read_j_immediate(u32 raw) {
    // — inst[31] — inst[19:12] inst[20] inst[30:25] inst[24:21] 0
    union {
        struct {
            u32 zero : 1;
            u32 inst_24_21 : 4;
            u32 inst_30_25 : 6;
            u32 inst_20 : 1;
            u32 inst_19_12 : 8;
            u32 inst_31 : 12;
        } PACKED;
        u32 raw;
    } imm;

    union {
        struct {
            u32 zero : 12;
            u32 inst_19_12 : 8;
            u32 inst_20 : 1;
            u32 inst_24_21 : 4;
            u32 inst_30_25 : 6;
            u32 inst_31 : 1;
        } PACKED;
        u32 raw;
    } extract;

    extract.raw = raw;

    imm.zero = 0;
    imm.inst_19_12 = extract.inst_19_12;
    imm.inst_20 = extract.inst_20;
    imm.inst_24_21 = extract.inst_24_21;
    imm.inst_30_25 = extract.inst_30_25;
    // ensure all bits are set to the same bit to perform sign extension.
    imm.inst_31 = 0ul - extract.inst_31;

    return imm.raw;
}

static u32 read_b_immediate(u32 raw) {
    // — inst[31] — inst[7] inst[30:25] inst[11:8] 0

    union {
        struct {
            u32 zero : 1;
            u32 inst_11_8 : 4;
            u32 inst_30_25 : 6;
            u32 inst_7 : 1;
            u32 inst_31 : 20;
        } PACKED;
        u32 raw;
    } imm;

    union {
        struct {
            u32 zero_below : 6;
            u32 inst_7 : 1;
            u32 inst_11_8 : 4;
            u32 unused_inner : 14;
            u32 inst_30_25 : 6;
            u32 inst_31 : 1;
        } PACKED;
        u32 raw;
    } extract;

    extract.raw = raw;

    imm.raw = 0;
    imm.zero = 0;
    imm.inst_7 = extract.inst_7;
    imm.inst_11_8 = extract.inst_11_8;
    imm.inst_31 = 0ul - extract.inst_31;

    return imm.raw;
}
