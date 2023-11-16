#include "dasm.h"
#include "insn.h"
#include <assert.h>
#include <stdio.h>

void dasm(FILE *out, u32 raw) {
    union {
        uint32_t raw;
        struct insn in;
    } as;

    as.raw = raw;

    assert(!"TODO: disassemble");
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
