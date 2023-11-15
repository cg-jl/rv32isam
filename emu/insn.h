//
// Created by cg on 10/17/23.
//

#pragma once

#include "../common/types.h"

// RISC-V Specification:
// https://riscv.org/wp-content/uploads/2017/05/riscv-spec-v2.2.pdf

// RISC-V Specification, Table 19.1: RISC-V base opcode map.
enum insn_op {
    op_load = 0b00000,
    op_load_fp = 0b00001,
    op_custom_0 = 0b00010,
    op_misc_mem = 0b00011,
    op_imm = 0b00100,
    op_auipc = 0b00101,
    op_imm_32 = 0b00110,
    op_store = 0b01000,
    op_store_fp = 0b01001,
    op_custom_1 = 0b01010,
    op_amo = 0b01011,
    op_op = 0b01100,
    op_lui = 0b01101,
    op_op_32 = 0b01110,
    op_madd = 0b10000,
    op_msub = 0b10001,
    op_nmsub = 0b10010,
    op_nmadd = 0b10011,
    op_fp = 0b10100,
    // reserved: 0b10100
    op_custom2_rv128 = 0b10110,
    op_branch = 0b11000,
    op_jalr = 0b11001,
    // reserved: 0b11010
    op_system = 0b11100,
    // reserved: 0b11101
    op_custom3_rv128 = 0b11110,
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

struct insn {
    union {
        struct {
            enum insn_op opcode : 7;
            u32 unk_rest : 25;
        } unknown;
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
        } __attribute__((packed)) b;
        struct {
            enum insn_op opcode : 7;
            u8 rd : 5;
            u32 imm_31_12 : 20;
        } __attribute__((packed)) u;
        struct {
            enum insn_op opcode : 7;
            u8 rd : 5;
            u8 imm_19_12 : 8;
            u8 imm_11 : 1;
            u16 imm_10_1 : 10;
            u8 imm_20 : 1;
        } __attribute__((packed)) j;
    } __attribute__((packed));
} __attribute__((packed));

// vim:ft=c
