#include "dasm.h"
#include "insn.h"
#include <assert.h>
#include <stdio.h>

static char const *abi_reg_names[];

void dasm(FILE *out, u32 raw) {

    union insn as;

    as.raw = raw;

    switch (as.unknown.opcode) {
    case op_load: {
        static char const *func_names[] = {
            [load_func_lb] = "lb",   [load_func_lh] = "lh",
            [load_func_lw] = "lw",   [load_func_lbu] = "lbu",
            [load_func_lhu] = "lhu",
        };

        fprintf(out, "%s %s, %d(%s)\n", func_names[as.i.funct3],
                abi_reg_names[as.i.rd], as.i.imm_11_0, abi_reg_names[as.i.rs1]);
    } break;
    case op_branch: {
        static char const *branch_names[] = {
            [branch_func_beq] = "beq",   [branch_func_bne] = "bne",
            [branch_func_blt] = "blt",   [branch_func_bge] = "bge",
            [branch_func_bltu] = "bltu", [branch_func_bgeu] = "bgeu",
        };

        u32 imm = ((u32)as.b.imm_12 << 12 | (u32)as.b.imm_11 << 11 |
                   (u32)as.b.imm_10_5 << 5 | (u32)as.b.imm_4_1 << 1);
        fprintf(out, "%s %s, %s, %x\n", branch_names[as.b.funct3],
                abi_reg_names[as.b.rs1], abi_reg_names[as.b.rs2], imm);
        break;
    }
    case op_load_fp:
    case op_custom_0:
    case op_misc_mem:
    case op_imm:
    case op_auipc:
    case op_imm_32:
    case op_store:
    case op_store_fp:
    case op_custom_1:
    case op_amo:
    case op_op:
    case op_lui:
    case op_op_32:
    case op_madd:
    case op_msub:
    case op_nmsub:
    case op_nmadd:
    case op_fp:
    case op_custom2_rv128:
    case op_jalr:
    case op_system:
    case op_custom3_rv128:
        assert(!"TODO: disassemble");
    }
}

char const *opcode_names[] = {
    [0b0000011] = "op_load",
    [0b0000111] = "op_load_fp",
    [0b0001011] = "op_custom_0",
    [0b0001111] = "op_misc_mem",
    [0b0010011] = "op_imm",
    [0b0010111] = "op_auipc",
    [0b0011011] = "op_imm_32",
    [0b0100011] = "op_store",
    [0b0100111] = "op_store_fp",
    [0b0101011] = "op_custom_1",
    [0b0101111] = "op_amo",
    [0b0110011] = "op_op",
    [0b0110111] = "op_lui",
    [0b0111011] = "op_op_32",
    [0b1000011] = "op_madd",
    [0b1000111] = "op_msub",
    [0b1001011] = "op_nmsub",
    [0b1001111] = "op_nmadd",
    [0b1010011] = "op_fp",
    [0b1010111] = "reserved: 0b10100",
    [0b1011011] = "op_custom2_rv128",
    [0b1100011] = "op_branch",
    [0b1100111] = "op_jalr",
    [0b1101011] = "reserved: 0b11010",
    [0b1110011] = "op_system",
    [0b1110111] = "reserved:  0b11101",
    [0b1111011] = "op_custom3_rv128",
};
static char const *abi_reg_names[] = {
    [0] = "zero", [1] = "ra", [2] = "sp", [3] = "gp",
    [4] = "tp",   [5] = "t0", [6] = "t1", [7] = "t2",
};
