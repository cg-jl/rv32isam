#include "dasm.h"
#include "../common/log.h"
#include "bits.h"
#include "insn.h"
#include <assert.h>
#include <stdio.h>

static char const *abi_reg_names[];

void dasm(FILE *out, u32 raw, u32 insn_offset) {

    if (__builtin_expect(raw == 0, 0)) {
        fputs("<illegal>", out);
    }

    union insn as;

    as.raw = raw;

    // 0b1101111

    switch (as.unknown.opcode) {
    case op_jal: {
        fprintf(out, "jal %s, 0x%x", abi_reg_names[as.j.rd],
                insn_offset + bit_cast_i32(read_j_immediate(as.raw)));
    } break;
    case op_auipc: {
        fprintf(out, "auipc 0x%x", read_upper_immediate(as.raw));
    } break;
    case op_load: {
        static char const *func_names[] = {
            [load_func_lb] = "lb",   [load_func_lh] = "lh",
            [load_func_lw] = "lw",   [load_func_lbu] = "lbu",
            [load_func_lhu] = "lhu",
        };

        fprintf(out, "%s %s, %d(%s)", func_names[as.i.funct3],
                abi_reg_names[as.i.rd], as.i.imm_11_0, abi_reg_names[as.i.rs1]);
    } break;
    case op_branch: {
        static char const *branch_names[] = {
            [branch_func_beq] = "beq",   [branch_func_bne] = "bne",
            [branch_func_blt] = "blt",   [branch_func_bge] = "bge",
            [branch_func_bltu] = "bltu", [branch_func_bgeu] = "bgeu",
        };

        i32 imm = bit_cast_i32(read_b_immediate(as.raw));
        fprintf(out, "%s %s, %s, 0x%x", branch_names[as.b.funct3],
                abi_reg_names[as.b.rs1], abi_reg_names[as.b.rs2],
                insn_offset + imm);
        break;
    }
    case op_lui:
        fprintf(out, "lui %s, %x", abi_reg_names[as.u.rd], as.u.imm_31_12);
        break;
    case op_imm: {
        static char const *imm_names[] = {
            [imm_func_addi] = "addi",
            [imm_func_slti] = "slti",
            [imm_func_sltiu] = "sltiu",
            [imm_func_xori] = "xori",
            [imm_func_ori] = "ori",
            [imm_func_andi] = "andi",

            // These used a specialized I-type format, in which the 5 low
            // bits of the immediate is the shift amount, and the rest is
            // fixed. The fixed values are in `enum shift_func`, and they're
            // already shifted.

            [imm_func_slli] = "slli",
            [imm_func_srli] = "srli",
        };

        switch ((enum insn_imm_func)as.i.funct3) {
        case imm_func_addi:
        case imm_func_slti:
        case imm_func_sltiu:
        case imm_func_xori:
        case imm_func_ori:
        case imm_func_andi:

            fprintf(out, "%s %s, %s, 0x%x", imm_names[as.i.funct3],
                    abi_reg_names[as.i.rd], abi_reg_names[as.i.rs1],
                    read_shift_immediate(as.raw));
            break;

        case imm_func_slli:
            fprintf(out, "slli %s, %s, 0x%x", abi_reg_names[as.i.rd],
                    abi_reg_names[as.i.rs1], read_shift_immediate(as.raw));
            break;
        case imm_func_srli:
            assert(!"TODO: disassemble sr[la]i instructions");
            /* TODO: case imm_func_srai */
            break;
        }
    } break;

    case op_store: {
        static char const *modes[] = {
            [store_func_sb] = "sb",
            [store_func_sh] = "sh",
            [store_func_sw] = "sw",
        };

        i32 imm = bit_cast_i32(read_s_immediate(as.raw));

        fprintf(out, "%s %s, %s0x%x(%s)", modes[as.s.funct3],
                abi_reg_names[as.s.rs2], imm < 0 ? "-" : "-", imm,
                abi_reg_names[as.s.rs1]);
    } break;
    case op_op:
        // Classified on "has func7? [yes no]"
        {
            static char const *inames[][2] = {
                [op_funct3_add] = {"add", "sub"},
                [op_funct3_sll] = {"sll", "sll"},
                [op_funct3_slt] = {"slt", "slt"},
                [op_funct3_sltu] = {"sltu", "sltu"},
                [op_funct3_xor] = {"xor", "xor"},
                [op_funct3_srl] = {"srl", "sra"},
                [op_funct3_or] = {"or", "or"},
                [op_funct3_and] = {"and", "and"},
            };

            u32 has_funct7 = as.r.funct7 != 0;

            fprintf(out, "%s %s, %s, %s", inames[as.r.funct3][has_funct7],
                    abi_reg_names[as.r.rd], abi_reg_names[as.r.rs1],
                    abi_reg_names[as.r.rs2]);
        }
        break;
    case op_system:
        switch ((enum ecall_imm)as.i.imm_11_0) {
        case ecall_ecall:
            fputs("ecall", out);
            break;
        case ecall_ebreak:
            fputs("ebreak", out);
            break;
        }
        break;

    case op_load_fp:
    case op_custom_0:
    case op_misc_mem:
    case op_imm_32:
    case op_store_fp:
    case op_custom_1:
    case op_amo:
    case op_op_32:
    case op_madd:
    case op_msub:
    case op_nmsub:
    case op_nmadd:
    case op_fp:
    case op_custom2_rv128:
    case op_jalr:
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
    [rv_zero] = "zero", [rv_ra] = "ra", [rv_sp] = "sp",   [rv_gp] = "gp",
    [rv_tp] = "tp",     [rv_t0] = "t0", [rv_t1] = "t1",   [rv_t2] = "t2",
    [rv_s0] = "s0",     [rv_s1] = "s1", [rv_a0] = "a0",   [rv_a1] = "a1",
    [rv_a2] = "a2",     [rv_a3] = "a3", [rv_a4] = "a4",   [rv_a5] = "a5",
    [rv_a6] = "a6",     [rv_a7] = "a7", [rv_s2] = "s2",   [rv_s3] = "s3",
    [rv_s4] = "s4",     [rv_s5] = "s5", [rv_s6] = "s6",   [rv_s7] = "s7",
    [rv_s8] = "s8",     [rv_s9] = "s9", [rv_s10] = "s10", [rv_s11] = "s11",
    [rv_t3] = "t3",     [rv_t4] = "t4", [rv_t5] = "t5",   [rv_t6] = "t6",

};
