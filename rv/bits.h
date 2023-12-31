#pragma once
#include "../common/types.h"
#include "insn.h"

static i32 __attribute_const__ bit_cast_i32(u32 v) { return *(i32 *)&v; }
static u32 __attribute_const__ bit_cast_u32(i32 v) {
    union {
        i32 from;
        u32 to;
    } as;
    as.from = v;
    return as.to;
}

static i32 __attribute_const__ make_positive(i32 v) { return v < 0 ? -v : v; }

static i32 __attribute_const__ sext32_imm32(u32 x) { return bit_cast_i32(x); }

static i32 __attribute_const__ sext32_generic(u32 v) {
    if (v == 0)
        return 0;
    u32 fill_count = __builtin_clz(v);
    u32 bit_pos = 32 - fill_count;
    u32 mask = (1 << (fill_count - 1)) << (bit_pos - 1);
    return v | mask;
}

static i32 __attribute_const__ sext32_imm12(u16 x_12) {
    u32 x = x_12;
    // find the 12th bit and extend it to 0 or all 1s
    u32 mask = ~((x >> 11) - 1);
    // only interested in the first (32 - 12 = 20) bits
    mask &= 0xfffff000;
    return bit_cast_i32(x_12 | mask);
}

static u32 __attribute_const__ read_upper_immediate(u32 raw) {
    return raw & 0xfffff000;
}

static u32 __attribute_const__ read_j_immediate(u32 raw) {
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

static u32 __attribute_const__ read_b_immediate(u32 raw) {

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
            u32 unused_below : 7;
            u32 inst_7 : 1;
            u32 inst_11_8 : 4;
            u32 unused_inner : 13; // 12-24
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
    imm.inst_30_25 = extract.inst_30_25;
    imm.inst_31 = 0ul - extract.inst_31;

    return imm.raw;
}

static u32 __attribute_const__ read_s_immediate(u32 raw) {
    union {
        struct {
            u32 inst_7 : 1;
            u32 inst_11_8 : 4;
            u32 inst_30_25 : 6;
            u32 inst_31 : 21;
        } PACKED;
        u32 raw;
    } imm;
    union {
        struct {
            u32 zero_below : 7;
            u32 inst_7 : 1;
            u32 inst_11_8 : 4;
            u32 unused_inner : 13;
            u32 inst_30_25 : 6;
            u32 inst_31 : 1;
        } PACKED;
        u32 raw;
    } extract;

    imm.raw = 0;
    extract.raw = raw;

    imm.inst_7 = extract.inst_7;
    imm.inst_11_8 = extract.inst_11_8;
    imm.inst_30_25 = extract.inst_30_25;
    imm.inst_31 = 0ul - extract.inst_31;

    return imm.raw;
}

static u32 __attribute_const__ read_i_immediate(u32 raw) {
    union {
        struct {
            u32 inst_20 : 1;
            u32 inst_24_21 : 4;
            u32 inst_30_25 : 6;
            u32 inst_31 : 21;
        } PACKED;
        u32 raw;
    } imm;

    union {
        struct {
            u32 low_20 : 20;
            u32 inst_20 : 1;
            u32 inst_24_21 : 4;
            u32 inst_30_25 : 6;
            u32 inst_31 : 1;
        } PACKED;
        u32 raw;
    } extract;

    imm.raw = 0;
    extract.raw = raw;

    imm.inst_20 = extract.inst_20;
    imm.inst_24_21 = extract.inst_24_21;
    imm.inst_30_25 = extract.inst_30_25;
    imm.inst_31 = 0ul - extract.inst_31;

    return imm.raw;
}

static u32 __attribute_const__ read_shift_immediate(u32 raw) {
    // lower 5 bits of the I-immediate field.
    return read_i_immediate(raw) & 0x1f;
}
