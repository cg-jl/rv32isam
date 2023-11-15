//
// Created by cg on 10/17/23.
//

#include "interpret.h"
#include "common/log.h"
#include "emu/dasm.h"
#include "emu/insn.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

struct rv32i {
    uint32_t registers[32];
};

static uint32_t sext32_imm32(uint32_t x) { return x; }

static uint32_t sext32_imm12(uint16_t x_12) {
    uint32_t x = x_12;
    // find the 12th bit and extend it to 0 or all 1s
    uint32_t mask = (x >> 11) - 1;
    // only interested in the first (32 - 12 = 20) bits
    mask &= 0xfffff000;
    return x_12 | mask;
}

static uint32_t read_upper_immediate(uint32_t raw) {
    return sext32_imm32(raw & 0xfffff000);
}

static int32_t bit_cast_i32(uint32_t v) { return *(int32_t *)&v; }

void interpret(void const *memory, uint32_t entrypoint) {
    struct rv32i cpu = {0};

    uint32_t pc = entrypoint;
    for (;; pc += 4) {
        union {
            struct insn insn;
            uint32_t raw;
        } as;
        uint32_t const *insn_ptr = memory + pc;
        if (__builtin_expect((uintptr_t)insn_ptr & 0b11, 0)) {
            fputs("fatal: instructions MUST be aligned to 4 bytes\n", stderr);
            abort();
        }
        as.raw = *(uint32_t *)(__builtin_assume_aligned(insn_ptr, 4));
        printf("insn: %x\n", as.raw);
        printf("opcode: %s\n", opcode_names[as.insn.unknown.opcode]);
        switch (as.insn.unknown.opcode) {
        case op_lui:
            // x[rd] = sext(immediate[31:12] << 12)
            // sext will be identity; since we're 32-bit.
            cpu.registers[as.insn.u.rd] = read_upper_immediate(as.raw);
            break;
        case op_auipc:
            // x[rd] = pc + sext(immediate[31:12] << 12)
            cpu.registers[as.insn.u.rd] = pc + read_upper_immediate(as.raw);
            break;
        case op_imm:
            switch ((enum insn_imm_func)as.insn.i.funct3) {
            case imm_func_addi:
                // x[rd] = x[rs1] + sext(immediate)
                cpu.registers[as.insn.i.rd] = cpu.registers[as.insn.i.rs1] +
                                              sext32_imm12(as.insn.i.imm_11_0);
                break;
            case imm_func_slti:
                // x[rd] = x[rs1] <s sext(immediate)
                {
                    int32_t signed_xrs1 =
                        bit_cast_i32(cpu.registers[as.insn.i.rs1]);
                    int32_t signed_imm =
                        bit_cast_i32(sext32_imm12(as.insn.i.imm_11_0));
                    // "cheat" by using an already implemented signed comparison
                    cpu.registers[as.insn.i.rd] = signed_xrs1 < signed_imm;
                }
                break;
            case imm_func_sltiu:
                // x[rd] = x[rs1] <u sext(immediate)
                // "cheat" by using an already implemented unsigned comparison
                cpu.registers[as.insn.i.rd] = cpu.registers[as.insn.i.rs1] <
                                              sext32_imm12(as.insn.i.imm_11_0);
                break;
            case imm_func_xori: // sorry :(
                // x[rd] = x[rs1] ^ sext(immediate)
                cpu.registers[as.insn.i.rd] = cpu.registers[as.insn.i.rs1] ^
                                              sext32_imm12(as.insn.i.imm_11_0);
                break;
            case imm_func_ori:
                // x[rd] = x[rs1] | sext(immediate)
                cpu.registers[as.insn.i.rd] = cpu.registers[as.insn.i.rs1] |
                                              sext32_imm12(as.insn.i.imm_11_0);
                break;
            case imm_func_andi:
                // x[rd] = x[rs1] & sext(immediate)
                cpu.registers[as.insn.i.rd] = cpu.registers[as.insn.i.rs1] &
                                              sext32_imm12(as.insn.i.imm_11_0);
                break;
            case imm_func_slli:
                // x[rd] = x[rs1] << shamt
                // shamt is lower five bits, since anything else would wrap
                // around.
                cpu.registers[as.insn.i.rd] = cpu.registers[as.insn.i.rs1]
                                              << (as.insn.i.imm_11_0 & 0x1f);
                break;
            case imm_func_srli: {
                u8 shift_count = as.insn.i.imm_11_0 & ((1 << 5) - 1);
                u32 rest = as.insn.i.imm_11_0 & ~((1 << 5) - 1);
                switch ((enum shift_func)rest) {
                case shift_func_srli:
                case shift_func_srai:
                    assert(!"not implemented");
                }
                break;
            }
            }
            break;
        case op_load:
        case op_load_fp:
        case op_custom_0:
        case op_misc_mem:
        case op_imm_32:
        case op_store:
        case op_store_fp:
        case op_custom_1:
        case op_amo:
        case op_op:
        case op_op_32:
        case op_madd:
        case op_msub:
        case op_nmsub:
        case op_nmadd:
        case op_fp:
        case op_custom2_rv128:
        case op_branch:
        case op_jalr:
        case op_system:
        case op_custom3_rv128:
            assert(!"not implemented");
        }
    }
}

// vim:sw=4
