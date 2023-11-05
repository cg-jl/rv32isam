//
// Created by cg on 10/17/23.
//

#ifndef SAM_INSN_H
#define SAM_INSN_H

#include <stdint.h>

enum insn_code {
    INSN_LUI = 0b0110111,
    INSN_AUIPC = 0b0010111,
    INSN_ADDI_SLTI = 0b0010011,
};

enum r_funct3_discriminator {
    FUNCT3_ADDI = 0b000,
    FUNCT3_SLTI = 0b010,
    FUNCT3_SLTIU = 0b011,
    FUNCT3_XORI = 0b100,
    FUNCT3_ORI = 0b110,
    FUNCT3_ANDI = 0b111,
    FUNCT3_SLLI = 0b001,
};


union insn {
    uint32_t raw;
    struct {
        enum insn_code code: 6;
        union {
            struct {
                uint8_t rd: 5;
                uint32_t imm: 20;
            } u;
            struct {
                uint16_t imm: 12;
                uint8_t rs1: 5;
                enum r_funct3_discriminator funct3: 3;
                uint8_t rd: 5;
            } i;
        };
    };
};

#endif //SAM_INSN_H
