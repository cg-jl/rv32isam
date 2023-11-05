//
// Created by cg on 10/17/23.
//

#include "interpret.h"
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
        union insn insn;
        uint32_t const *insn_ptr = memory + pc;
        if (__builtin_expect((uintptr_t)insn_ptr & 0b11, 0)) {
            fputs("fatal: instructions MUST be aligned to 4 bytes\n", stderr);
            abort();
        }
        insn.raw = *(uint32_t *)(__builtin_assume_aligned(insn_ptr, 4));
        printf("insn: %x\n", insn.raw);
        printf("opcode: %x\n", insn.code);
        switch (insn.code) {
        case INSN_LUI:
            // x[rd] = sext(immediate[31:12] << 12)
            // sext will be identity; since we're 32-bit.
            cpu.registers[insn.u.rd] = read_upper_immediate(insn.raw);
            break;
        case INSN_AUIPC:
            // x[rd] = pc + sext(immediate[31:12] << 12)
            cpu.registers[insn.u.rd] = pc + read_upper_immediate(insn.raw);
            break;
        case INSN_ADDI_SLTI:
            switch (insn.i.funct3) {
            case FUNCT3_ADDI:
                // x[rd] = x[rs1] + sext(immediate)
                cpu.registers[insn.i.rd] =
                    cpu.registers[insn.i.rs1] + sext32_imm12(insn.i.imm);
                break;
            case FUNCT3_SLTI:
                // x[rd] = x[rs1] <s sext(immediate)
                {
                    int32_t signed_xrs1 =
                        bit_cast_i32(cpu.registers[insn.i.rs1]);
                    int32_t signed_imm = bit_cast_i32(sext32_imm12(insn.i.imm));
                    // "cheat" by using an already implemented signed comparison
                    cpu.registers[insn.i.rd] = signed_xrs1 < signed_imm;
                }
                break;
            case FUNCT3_SLTIU:
                // x[rd] = x[rs1] <u sext(immediate)
                // "cheat" by using an already implemented unsigned comparison
                cpu.registers[insn.i.rd] =
                    cpu.registers[insn.i.rs1] < sext32_imm12(insn.i.imm);
                break;
            case FUNCT3_XORI: // sorry :(
                // x[rd] = x[rs1] ^ sext(immediate)
                cpu.registers[insn.i.rd] =
                    cpu.registers[insn.i.rs1] ^ sext32_imm12(insn.i.imm);
                break;
            case FUNCT3_ORI:
                // x[rd] = x[rs1] | sext(immediate)
                cpu.registers[insn.i.rd] =
                    cpu.registers[insn.i.rs1] | sext32_imm12(insn.i.imm);
                break;
            case FUNCT3_ANDI:
                // x[rd] = x[rs1] & sext(immediate)
                cpu.registers[insn.i.rd] =
                    cpu.registers[insn.i.rs1] & sext32_imm12(insn.i.imm);
                break;
            case FUNCT3_SLLI:
                // x[rd] = x[rs1] << shamt
                // shamt is lower five bits, since anything else would wrap
                // around.
                cpu.registers[insn.i.rd] = cpu.registers[insn.i.rs1]
                                           << (insn.i.imm & 0x1f);
                break;
            default:
                assert(!"Unknown funct3 code");
            }
            break;
        default:
            assert(!"Unknown insn opcode");
        }
    }
}

// vim:sw=4
