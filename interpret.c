//
// Created by cg on 10/17/23.
//

#include "interpret.h"
#include "common/log.h"
#include "rv/dasm.h"
#include "rv/insn.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

struct rv32i {
    uint32_t registers[32];
};
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

void interpret(void *memory, u32 entrypoint) {
    struct rv32i cpu = {0};

    u32 pc = entrypoint;
    for (;; pc += 4) {
        union insn as;
        u32 const *insn_ptr = memory + pc;
        if (__builtin_expect((uintptr_t)insn_ptr & 0b11, 0)) {
            fputs("fatal: instructions MUST be aligned to 4 bytes\n", stderr);
            abort();
        }
        as.raw = *(u32 *)(__builtin_assume_aligned(insn_ptr, 4));
        log("insn @ 0x%08x: (0x%08x) ", pc, as.raw);
        dasm(stderr, as.raw);
        fputc('\n', stderr);
        if (as.raw == 0) {
            error("Refusing to execute: illegal all 0s instruction\n");
            __builtin_trap();
            return;
        }
        switch (as.unknown.opcode) {
        case op_lui:
            // x[rd] = sext(immediate[31:12] << 12)
            // sext will be identity; since we're 32-bit.
            cpu.registers[as.u.rd] = read_upper_immediate(as.raw);
            break;
        case op_auipc:
            // x[rd] = pc + sext(immediate[31:12] << 12)
            cpu.registers[as.u.rd] = pc + read_upper_immediate(as.raw);
            break;
        case op_imm:
            switch ((enum insn_imm_func)as.i.funct3) {
            case imm_func_addi:
                // x[rd] = x[rs1] + sext(immediate)
                cpu.registers[as.i.rd] = bit_cast_i32(cpu.registers[as.i.rs1]) +
                                         sext32_imm12(as.i.imm_11_0);
                break;
            case imm_func_slti:
                // x[rd] = x[rs1] <s sext(immediate)
                {
                    int32_t signed_xrs1 = bit_cast_i32(cpu.registers[as.i.rs1]);
                    int32_t signed_imm =
                        bit_cast_i32(sext32_imm12(as.i.imm_11_0));
                    // "cheat" by using an already implemented signed comparison
                    cpu.registers[as.i.rd] = signed_xrs1 < signed_imm;
                }
                break;
            case imm_func_sltiu:
                // x[rd] = x[rs1] <u sext(immediate)
                // "cheat" by using an already implemented unsigned comparison
                cpu.registers[as.i.rd] =
                    cpu.registers[as.i.rs1] < sext32_imm12(as.i.imm_11_0);
                break;
            case imm_func_xori: // sorry :(
                // x[rd] = x[rs1] ^ sext(immediate)
                cpu.registers[as.i.rd] =
                    cpu.registers[as.i.rs1] ^ sext32_imm12(as.i.imm_11_0);
                break;
            case imm_func_ori:
                // x[rd] = x[rs1] | sext(immediate)
                cpu.registers[as.i.rd] =
                    cpu.registers[as.i.rs1] | sext32_imm12(as.i.imm_11_0);
                break;
            case imm_func_andi:
                // x[rd] = x[rs1] & sext(immediate)
                cpu.registers[as.i.rd] =
                    cpu.registers[as.i.rs1] & sext32_imm12(as.i.imm_11_0);
                break;
            case imm_func_slli:
                // x[rd] = x[rs1] << shamt
                // shamt is lower five bits, since anything else would wrap
                // around.
                cpu.registers[as.i.rd] = cpu.registers[as.i.rs1]
                                         << (as.i.imm_11_0 & 0x1f);
                break;
            case imm_func_srli: {
                u8 shift_count = as.i.imm_11_0 & ((1 << 5) - 1);
                u32 rest = as.i.imm_11_0 & ~((1 << 5) - 1);
                switch ((enum shift_func)rest) {
                case shift_func_srli:
                case shift_func_srai:
                    assert(!"not implemented");
                }
                break;
            }
            }
            break;
        case op_load: {
            i32 offset = *(i32 *)&cpu.registers[as.i.rs1];
            void const *mem_loc = memory + offset;
            mem_loc += sext32_imm12(as.i.imm_11_0);
            switch ((enum insn_load_func)as.i.funct3) {
            case load_func_lbu: {
                cpu.registers[as.i.rd] = *(uint8_t *)mem_loc;
                break;
            }
            case load_func_lb:
            case load_func_lh:
            case load_func_lw:
            case load_func_lhu:
                assert(!"not implemented load");
            }
        } break;

        case op_store: {
            i32 offset =
                sext32_imm12((u32)as.s.imm_4_0 | (u32)as.s.imm_11_5 << 5);

            void *mem_loc =
                memory + offset + bit_cast_i32(cpu.registers[as.s.rs1]);

            switch ((enum insn_store_func)as.s.funct3) {
            case store_func_sb:
                *(uint8_t *)mem_loc = cpu.registers[as.s.rs2];
                break;
            case store_func_sh:
            case store_func_sw:
                assert(!"not implemented store");
            }

        } break;
        case op_branch: {

            i32 offset =
                2 * sext32_generic((u32)as.b.imm_11 << 11 |
                                   (u32)as.b.imm_12 << 12 |
                                   (u32)as.s.imm_11_5 << 5 | (u32)as.s.imm_4_0);

            u32 a = cpu.registers[as.b.rs1];
            u32 b = cpu.registers[as.b.rs2];

            switch ((enum insn_branch_func)as.b.funct3) {
            case branch_func_beq:
                if (a == b)
                    pc += offset - 4;
                break;
            case branch_func_bne:
            case branch_func_blt:
            case branch_func_bge:
            case branch_func_bltu:
            case branch_func_bgeu:
                assert(!"not implemented branch");
            }
        } break;

        case op_op: {
            switch ((enum insn_op_funct3)as.r.funct3) {
            case op_funct3_or:
                cpu.registers[as.r.rd] =
                    cpu.registers[as.r.rs1] | cpu.registers[as.r.rs2];
                break;
            case op_funct3_add:
            case op_funct3_sll:
            case op_funct3_slt:
            case op_funct3_sltu:
            case op_funct3_xor:
            case op_funct3_srl:
            case op_funct3_and:
                assert(!"not implemented r-type op.");
            }
        }; break;

        case op_system:
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
            assert(!"not implemented");
        }
    }
}

// vim:sw=4
