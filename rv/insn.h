//
// Created by cg on 10/17/23.
//

#pragma once

#include "../common/types.h"

// RISC-V Specification:
// https://riscv.org/wp-content/uploads/2017/05/riscv-spec-v2.2.pdf

// https://en.wikichip.org/wiki/risc-v/registers
enum rv32_abi_reg {
    rv_zero = 0,
    rv_ra = 1,
    rv_sp = 2,
    rv_gp = 3,
    rv_tp = 4,
    rv_t0 = 5,
    rv_t1 = 6,
    rv_t2 = 7,
    rv_s0 = 8,
    rv_fp = 8,
    rv_s1 = 9,
    rv_a0 = 10,
    rv_a1 = 11,
    rv_a2 = 12,
    rv_a3 = 13,
    rv_a4 = 14,
    rv_a5 = 15,
    rv_a6 = 16,
    rv_a7 = 17,
    rv_s2 = 18,
    rv_s3 = 19,
    rv_s4 = 20,
    rv_s5 = 21,
    rv_s6 = 22,
    rv_s7 = 23,
    rv_s8 = 24,
    rv_s9 = 25,
    rv_s10 = 26,
    rv_s11 = 27,
    rv_t3 = 28,
    rv_t4 = 29,
    rv_t5 = 30,
    rv_t6 = 31,
};

// RISC-V Specification, Table 19.1: RISC-V base opcode map.
enum insn_op {
    // I-type.
    // Check funct3 as enum insn_load_func.
    op_load = 0b0000011,
    op_load_fp = 0b0000111,
    op_custom_0 = 0b0001011,
    op_misc_mem = 0b0001111,
    op_imm = 0b0010011,
    op_auipc = 0b0010111,
    op_imm_32 = 0b0011011,
    // S-type. Look at funct3 as an enum insn_store_func
    op_store = 0b0100011,
    op_store_fp = 0b0100111,
    op_custom_1 = 0b0101011,
    op_amo = 0b0101111,
    // R-type. func7 is op_funct7 and func3 is insn_op_funct3
    op_op = 0b0110011,
    // U-type.
    op_lui = 0b0110111,
    op_op_32 = 0b0111011,
    op_madd = 0b1000011,
    op_msub = 0b1000111,
    op_nmsub = 0b1001011,
    op_nmadd = 0b1001111,
    op_fp = 0b1010011,
    // reserved: 0b1010011
    op_custom2_rv128 = 0b1011011,
    op_branch = 0b1100011,
    op_jalr = 0b1100111,
    // reserved: 0b1101011
    op_jal = 0b1101111,

    // ecall/ebreak is encoded via a I-type. Imm_11_0 is encoded by ecall_imm
    op_system = 0b1110011,
    // reserved: 0b1110111
    op_custom3_rv128 = 0b1111011,
};

// The following are set in the funct3 bitfield, or otherwise if specified.

// Accompanied with an op_store opcode.
// S-type.
enum insn_store_func {
    store_func_sb = 0b000,
    store_func_sh = 0b001,
    store_func_sw = 0b010,
};

// Accompanied with an op_branch opcode.
// B-type.
enum insn_branch_func {
    branch_func_beq = 0b000,
    branch_func_bne = 0b001,
    branch_func_blt = 0b100,
    branch_func_bge = 0b101,
    branch_func_bltu = 0b110,
    branch_func_bgeu = 0b111,
};

// Accompanied with an op_load opcode.
// I-type.
enum insn_load_func {
    load_func_lb = 0b000,
    load_func_lh = 0b001,
    load_func_lw = 0b010,
    load_func_lbu = 0b100,
    load_func_lhu = 0b101,
};

// Accompanied with an op_imm opcode.
// I-type.
enum insn_imm_func {
    imm_func_addi = 0b000,
    imm_func_slti = 0b010,
    imm_func_sltiu = 0b011,
    imm_func_xori = 0b100,
    imm_func_ori = 0b110,
    imm_func_andi = 0b111,

    // These used a specialized I-type format, in which the 5 low bits of the
    // immediate is the shift amount, and the rest is fixed.
    // The fixed values are in `enum shift_func`, and they're already
    // shifted.

    imm_func_slli = 0b001,
    imm_func_srli = 0b101,
    imm_func_srai = 0b101,
};

// These go in the upper 7 bits of he 12-bit immediate of the I-type
// instruction.
enum shift_func {
    shift_func_srli = 0,
    shift_func_srai = 1 << 5,
};

// Accompanied with an op_op opcode, and funct7 is `enum insn_op_funct7`
// R-type.
enum insn_op_funct3 {
    op_funct3_add,
    // sub is func7
    op_funct3_sll,
    op_funct3_slt,
    op_funct3_sltu,
    op_funct3_xor,
    op_funct3_srl,
    // sra is func7
    op_funct3_or,
    op_funct3_and,
};

enum insn_op_funct7 {
    op_funct7_add = 0,
    op_funct7_sub = 1 << 5,
    op_funct7_sll = 0,
    op_funct7_slt = 0,
    op_funct7_sltu = 0,
    op_funct7_xor = 0,
    op_funct7_srl = 0,
    op_funct7_sra = 1 << 5,
    op_funct7_or = 0,
    op_funct7_and = 0,
};

// CSR instructions. Accompanied with an op_system opcode.
// I-type.
enum insn_csr_funct3 {
    csrrw,
    csrrs,
    csrrc,
    csrrwi,
    csrrsi,
    csrrci,
};

enum csr_special_source {
    rdcycle,
    rdcycle_h,
    rdtime,
    rdtime_h,
    rdinstret,
    rdinstret_h,
};

enum insn_fence_funct3 {
    // FENCE
    fence_funct3 = 0,
    // FENCE.I
    fence_funct3_i = 1,

};

// ecall/ebreak is encoded via a I-type. Imm_11_0 is encoded by ecall_imm
enum ecall_imm {
    ecall_ecall = 0,
    ecall_ebreak = 1 << 5,
};

enum fence_flags {
    fence_i = 1 << 3,
    fence_o = 1 << 2,
    fence_r = 1 << 1,
    fence_w = 1 << 0,
};

// These go in the func7 bitfield of the R-type instruction.

// diff between U and J :
// diff between S and B : 12-bit imm == multiples of 2 in B format.
// The middle bits imm[10:1] stay fixed from the instruction, and the lowest bit
// in S format (inst[7]) encodes a high-order bit in B format.

union insn {
    struct {
        enum insn_op opcode : 7;
        u32 unk_rest : 25;
    } __attribute__((packed)) unknown;

    // opcode is always op_misc_mem and funct3 is always FENCE.
    // everything that is not
    struct {
        enum insn_op opcode : 7;
        u8 rd : 5;
        u8 funct3 : 3;
        u8 rs1 : 3;
        enum fence_flags successor : 4;
        enum fence_flags predecessor : 4;
        u8 always_zero : 4;
    } __attribute__((packed)) fence;
    struct {
        enum insn_op opcode : 7;
        u8 rd : 5;
        u8 funct3 : 3;
        u8 rs1 : 5;
        u8 rs2 : 5;
        u8 funct7 : 7;
    } __attribute__((packed)) r;
    struct {
        enum insn_op opcode : 7;
        u8 rd : 5;
        u8 funct3 : 3;
        u8 rs1 : 5;
        u32 imm_11_0 : 12;
    } __attribute__((packed)) i;

    struct {
        enum insn_op opcode : 7;
        u8 imm_4_0 : 5;
        u8 funct3 : 3;
        u8 rs1 : 5;
        u8 rs2 : 5;
        u8 imm_11_5 : 7;
    } __attribute__((packed)) s;
    struct {
        enum insn_op opcode : 7;
        u8 imm_11 : 1;
        u8 imm_4_1 : 4;
        u8 funct3 : 3;
        u8 rs1 : 5;
        u8 rs2 : 5;
        u8 imm_10_5 : 6;
        u8 imm_12 : 1;
    } __attribute__((packed))
    // B-type.
    // The final value is a multiple of two bytes.
    b;
    struct {
        enum insn_op opcode : 7;
        u8 rd : 5;
        u32 imm_31_12 : 20;
    } __attribute__((packed)) u;
    struct j_format {
        enum insn_op opcode : 7;
        u8 rd : 5;
        u8 imm_19_12 : 8;
        u8 imm_11 : 1;
        u16 imm_10_1 : 10;
        u8 imm_20 : 1;
    } __attribute__((packed)) j;
    u32 raw;
} __attribute__((packed));

// vim:ft=c
