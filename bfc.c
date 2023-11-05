// q&d brainfuck -> rv32i compiler.
// The code has a tape of 3K to work with.
#include "bfc/out.h"
#include "common/types.h"
#include <elf.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// TODO: move asm to its own module
// TODO: use emu/insns.h instruction format definitions
// when generating/patching
// TODO: `patch` could be the same patch stuff as the ones that the loader
// should implement.

static void push(struct out *out, u32 val);
static u32 pop(struct out *out);

static void asm_empty_beq(struct out *out, u8 rs1, u8 rs2);

static void patch_beq(void *insn, i16 offset);
static void asm_jal(struct out *out, u8 link_register, i32 pc_rel_off);
static void asm_ecall(struct out *out);
static void asm_addi(struct out *out, u8 dest, u8 a, i16 imm);
static void asm_lbu(struct out *out, u8 base, u8 dest, u16 offset);
static void asm_sb(struct out *out, u8 base, u8 src, u16 offset);
static void asm_or(struct out *out, u8 dest, u8 a, u8 b);

#define t0 5
#define s1 9
#define zero 0
#define a0 10
#define a1 11
#define a2 12
#define a3 13
#define a4 14
#define a5 15
#define a7 17

static void compile_stdin(struct out *insns);

static u32 strtab_add(struct out *strtab, char const *name);

int main(void) {
    // set stdin to unbuffered so it doesn't ask line by line.
    setvbuf(stdin, NULL, _IONBF, 0);

    struct out insns = {0};
    struct out loop_begin_stack = {0};

    compile_stdin(&insns);

    // Write ELF
    struct out elf = {0};

    u32 page_size = sysconf(_SC_PAGESIZE);

    u32 ehdr_offt = out_resv_index(&elf, sizeof(Elf32_Ehdr));
    u32 text_segm = out_resv_index(&elf, sizeof(Elf32_Phdr));
    u32 data_segm = out_resv_index(&elf, sizeof(Elf32_Phdr));

    u32 text_shdr = out_resv_index(&elf, sizeof(Elf32_Shdr));
    u32 data_shdr = out_resv_index(&elf, sizeof(Elf32_Shdr));
    u32 rela_shdr = out_resv_index(&elf, sizeof(Elf32_Shdr));
    u32 symtab_shdr = out_resv_index(&elf, sizeof(Elf32_Shdr));
    u32 strtab_shdr = out_resv_index(&elf, sizeof(Elf32_Shdr));
    u32 shstrtab_shdr = out_resv_index(&elf, sizeof(Elf32_Shdr));

    u32 data_begin_virt = 0;
    u32 insns_begin_virt = 3 * 1024;

    u32 code_elf_begin = out_write_index(&elf, insns.bytes, insns.len);
    struct out shstrtab = {0};

    // finish text section
    {
        Elf32_Shdr *text = elf.bytes + text_shdr;
        text->sh_name = strtab_add(&shstrtab, ".text");
        text->sh_type = SHT_PROGBITS;
        text->sh_flags = SHF_ALLOC | SHF_EXECINSTR;
        text->sh_addr = insns_begin_virt;
        text->sh_offset = code_elf_begin;
        text->sh_size = insns.len;
        text->sh_link = text->sh_info = 0;
        text->sh_addralign = page_size;
        text->sh_entsize = 0;
    }
    // write text segment. The code should be aligned to 4 bytes, and
    // the virtual address is the start of the first segment.
    {
        Elf32_Phdr *text = elf.bytes + text_segm;
        text->p_type = PT_LOAD;
        text->p_offset = code_elf_begin;
        text->p_vaddr = insns_begin_virt;
        text->p_paddr = text->p_vaddr;
        text->p_filesz = insns.len;
        text->p_memsz = insns.len;
        text->p_flags = PF_R | PF_X;
        text->p_align = 4;
    }

    // finish a lazy data section that really doesn't do anything. it only
    // exists so that the symbol for _bf_data can refer to a section.
    {
        Elf32_Shdr *data = elf.bytes + data_shdr;
        data->sh_name = strtab_add(&shstrtab, ".data");
        data->sh_type = SHT_NOBITS;
        data->sh_flags = SHF_ALLOC | SHF_WRITE;
        data->sh_addr = data_begin_virt;
        data->sh_offset = code_elf_begin + insns.len;
        data->sh_size = 0;
        data->sh_link = data->sh_info = 0;
        data->sh_addralign = page_size;
        data->sh_entsize = 0;
    }

    // finish data segment. This declares 3KiB of zeroes.
    {
        Elf32_Phdr *data = elf.bytes + data_segm;
        data->p_type = PT_LOAD;
        // it's not in the file, but it would be here.
        data->p_offset = code_elf_begin + insns.len;
        data->p_vaddr = data_begin_virt;
        data->p_paddr = data->p_vaddr;
        data->p_filesz = 0;
        data->p_memsz = 3 * 1024;
        data->p_flags = PF_R | PF_W;
        data->p_align = page_size;
    }
    out_destroy(&insns);

    struct out strtab = {0};

    u32 symtab_elf_begin = out_resv_index(&elf, sizeof(Elf32_Sym) * 2);
    {
        Elf32_Sym *syms = elf.bytes + symtab_elf_begin;
        syms->st_name = strtab_add(&strtab, "_bf_data");
        syms->st_value = 0;
        syms->st_size = 3 * 1024;
        syms->st_info = ELF32_ST_INFO(STB_LOCAL, STT_OBJECT);
        syms->st_other = STV_DEFAULT;
        syms->st_shndx = 1; // .data
        // entrypoint
        syms[1].st_name = strtab_add(&strtab, "_bf_entry");
        syms[1].st_value = 0;
        syms[1].st_size = insns.len;
        syms[1].st_info = ELF32_ST_INFO(STB_LOCAL, STT_FUNC);
        syms[1].st_other = STV_DEFAULT;
        syms[1].st_shndx = 0; // .text
    }

    // finish symbol table
    {
        Elf32_Shdr *syms = elf.bytes + symtab_shdr;
        syms->sh_name = strtab_add(&shstrtab, ".symtab");
        syms->sh_type = SHT_SYMTAB;
        syms->sh_flags = 0;
        syms->sh_addr = 0;
        syms->sh_offset = symtab_elf_begin;
        syms->sh_size = 2 * sizeof(Elf32_Sym);
        syms->sh_info = syms->sh_link = 4; // .strtab
        syms->sh_addralign = 1;
        syms->sh_entsize = sizeof(Elf32_Sym);
    }

    // finish string table, which was only used for the symbol table.
    {
        u32 strtab_elf_begin = out_write_index(&elf, strtab.bytes, strtab.len);
        Elf32_Shdr *str = elf.bytes + strtab_shdr;
        str->sh_name = strtab_add(&shstrtab, ".strtab");
        str->sh_type = SHT_STRTAB;
        str->sh_flags = SHF_STRINGS;
        str->sh_addr = 0;
        str->sh_offset = strtab_elf_begin;
        str->sh_size = strtab.len;
        str->sh_link = str->sh_info = 0;
        str->sh_addralign = 1;
        str->sh_entsize = 1;
    }
    out_destroy(&strtab);

    u32 rela_offset = out_resv_index(&elf, sizeof(Elf32_Rela));
    {
        Elf32_Rela *rela = elf.bytes + rela_offset;
        // virtual address of storage unit affected by relocation: the first
        // instruction.
        rela->r_offset = insns_begin_virt + 0;
        // Symbol 0 is the only symbol we have and it's
        // the entrypoint symbol.
        rela->r_info = ELF32_R_INFO(0, R_RISCV_LO12_I);

        // S + A, where A == 0.
        rela->r_addend = 0;
    }

    // finish relocation table
    {
        Elf32_Shdr *rela = elf.bytes + rela_shdr;
        rela->sh_name = strtab_add(&shstrtab, ".rela.data");
        rela->sh_type = SHT_RELA;
        rela->sh_flags = 0;
        rela->sh_addr = 0;
        rela->sh_offset = rela_offset;
        rela->sh_size = sizeof(Elf32_Rela);
        rela->sh_link = 3; // link to symbol table
        rela->sh_info = 0;
        rela->sh_addralign = 0;
        rela->sh_entsize = sizeof(Elf32_Rela);
    }

    // finish shstrtab
    {
        // ensure we add the name to the table before copying the data.
        u32 name_index = strtab_add(&shstrtab, ".shstrtab");
        u32 shstrtab_elf_begin =
            out_write_index(&elf, shstrtab.bytes, shstrtab.len);
        Elf32_Shdr *shstr = elf.bytes + shstrtab_shdr;
        shstr->sh_name = name_index;
        shstr->sh_type = SHT_STRTAB;
        ;
        shstr->sh_flags = SHF_STRINGS;
        shstr->sh_addr = 0;
        shstr->sh_size = shstrtab.len;
        shstr->sh_offset = shstrtab_elf_begin;
        shstr->sh_link = shstr->sh_info = 0;
        shstr->sh_addralign = 1;
        shstr->sh_entsize = 1;

        out_destroy(&shstrtab);
    }

    // finish ELF header
    Elf32_Ehdr *eh = elf.bytes + ehdr_offt;

    memcpy(eh->e_ident + 1, "ELF", 3);
    eh->e_ident[EI_MAG0] = 0x7f;

    eh->e_ident[EI_CLASS] = ELFCLASS32;
    eh->e_ident[EI_DATA] = ELFDATA2LSB;
    eh->e_ident[EI_VERSION] = EV_CURRENT;
    eh->e_ident[EI_OSABI] = ELFOSABI_SYSV;
    eh->e_ident[EI_ABIVERSION] = 0;
    memset(&eh->e_ident[EI_PAD], 0, EI_NIDENT - EI_PAD);
    eh->e_type = ET_EXEC;
    eh->e_machine = EM_RISCV;
    eh->e_version = EV_CURRENT;
    eh->e_flags = 0;
    eh->e_phoff = text_segm;
    eh->e_shoff = text_shdr;
    eh->e_phnum = 2;
    eh->e_shnum = 6;
    eh->e_ehsize = sizeof(*eh);
    eh->e_shentsize = sizeof(Elf32_Shdr);
    eh->e_phentsize = sizeof(Elf32_Phdr);
    eh->e_shstrndx = 5;

    eh->e_entry = insns_begin_virt;

    // Write ELF image to output
    write(STDOUT_FILENO, elf.bytes, elf.len);
    out_destroy(&elf);
}

static void compile_stdin(struct out *insns) {
    // set stdin to unbuffered so getchar() doesn't go on lines.
    setvbuf(stdin, NULL, _IONBF, 0);

    struct out loop_begin_stack = {0};

    // Load the code address for the data segment, which has 3KiB
    // FIXME: This should be some sort of `lui` + `addi` mix, since addi is
    // restricted to 12-bit immediates.
    asm_addi(insns, s1, zero, 0);

    struct loop_info {
        u32 check_offset;
        u32 beq_offset;
    };

#define push_loop(chk, beq)                                                    \
    {                                                                          \
        struct loop_info *info = out_resv(insns, sizeof(struct loop_info));    \
        info->check_offset = chk;                                              \
        info->beq_offset = beq;                                                \
    }
#define pop_loop(loc)                                                          \
    {                                                                          \
        loop_begin_stack.len -= sizeof(struct loop_info);                      \
        loc = *(struct loop_info *)(loop_begin_stack.len +                     \
                                    loop_begin_stack.bytes);                   \
    }
#define load_cell(dest) asm_lbu(insns, s1, dest, 0)
#define store_cell(src) asm_sb(insns, s1, src, 0)

    {
        char c;
        while ((c = getchar()) != EOF) {
            switch (c) {
            case '[': {
                u32 check_offset = insns->len;
                // if [cell] != 0 goto loop_end (will be patched when finding
                // ']')
                load_cell(t0);
                push_loop(check_offset, insns->len);
                asm_empty_beq(insns, t0, zero);
                break;
            }
            case ']': {
                struct loop_info loop_info;
                pop_loop(loop_info);
                // We're going to write it right after, so no need to emit
                // dummy and then patch.
                u32 jal_offset = out_resv_index(insns, 4);
                u32 loop_end = insns->len;
                // HACK: Assuming that there are no overflows...
                i16 dist_from_chk = (i32)loop_end - (i32)loop_info.check_offset;

                // Skipping the loop body is skipping right here.
                patch_beq(insns->bytes + loop_info.beq_offset, dist_from_chk);

                // Make sure the loop body ends with a jump to the check.
                {
                    struct jal {
                        u8 tag : 7;
                        u8 rd : 5;
                        i32 offset : 20;
                    } __attribute__((packed)) *jal = insns->bytes + jal_offset;
                    jal->tag = 0b1101111;
                    jal->rd = zero;
                    // XXX: I don't know if this will interpret it correctly.
                    jal->offset = -dist_from_chk;
                }

                break;
            }
            case '+':
                load_cell(t0);
                asm_addi(insns, t0, t0, 1);
                store_cell(t0);
                break;
            case '-':
                load_cell(t0);
                asm_addi(insns, t0, t0, -1);
                store_cell(t0);
                break;
            case '.':
                // write(1, s1, 1);
                asm_addi(insns, a7, zero, 64);
                asm_addi(insns, a0, zero, 1);
                asm_or(insns, a0, zero, s1);
                asm_addi(insns, a2, zero, 1);
                asm_ecall(insns);
                // we don't care about the return value.
                break;
            case ',':
                // read(0, s1, 1);
                asm_addi(insns, a7, zero, 63);
                asm_addi(insns, a0, zero, 1);
                asm_or(insns, a0, zero, s1);
                asm_addi(insns, a2, zero, 1);
                asm_ecall(insns);
                // we don't care about the return value.
                break;
            case '>':
                asm_addi(insns, s1, s1, 1);
                break;
            case '<':
                asm_addi(insns, s1, s1, -1);
                break;
            }
        }
    }

#undef store_cell
#undef load_cell
#undef pop_loop
#undef push_loop

    out_destroy(&loop_begin_stack);
}

static u32 strtab_add(struct out *out, char const *name) {
    u32 length = strlen(name) + 1;
    return out_write_index(out, name, length);
}

static void out_write_u32lsb(struct out *out, u32 val) {
    *(u32 *)out_resv(out, 4) = val;
}

static void asm_or(struct out *out, u8 dest, u8 a, u8 b) {
    struct _or {
        u8 tag : 7;
        u8 rd : 5;
        u8 fixed_110 : 3;
        u8 rs1 : 5;
        u8 rs2 : 5;
        u8 fixed_zeros : 7;
    } __attribute__((packed)) *_or = out_resv(out, 4);
    _or->tag = 0b0110011;
    _or->fixed_110 = 0b110;
    _or->fixed_zeros = 0;
    _or->rd = dest;
    _or->rs1 = a;
    _or->rs2 = b;
}

static u32 pop(struct out *out) {
    out->len -= 4;
    return *(u32 *)(out->bytes + out->len);
}

static void push(struct out *out, u32 val) { out_write_u32lsb(out, val); }
#define mask(nbits) ((1 << (nbits)) - 1)

// asm_*

struct beq {
    u8 tag : 7;
    u8 offset_lower_5 : 5;
    u8 fixed_000 : 3;
    u8 rs1 : 5;
    u8 rs2 : 5;
    u8 offset_upper_7 : 7;
} __attribute__((packed));

static void asm_empty_beq(struct out *out, u8 rs1, u8 rs2) {
    struct beq *beq = out_resv(out, 4);
    beq->tag = 0b1100011;
    beq->rs1 = rs1;
    beq->rs2 = rs2;
    beq->fixed_000 = 0;
    beq->offset_upper_7 = 0;
    beq->offset_lower_5 = 0;
}
static void patch_beq(void *insn, i16 offset) {
    u16 offset_bits = *(u16 *)&offset & mask(12);
    struct beq *beq = (struct beq *)insn;
    beq->offset_lower_5 = offset_bits & mask(5);
    beq->offset_upper_7 = offset_bits >> 5;
}

static void asm_lbu(struct out *out, u8 base, u8 dest, u16 offset) {
    struct lbu {
        u8 tag : 7;
        u8 rd : 5;
        u8 fixed_100 : 3;
        u8 rs1 : 5;
        u16 offset : 12;
    } __attribute__((packed)) *lbu = out_resv(out, 4);
    lbu->tag = 0b0000011;
    lbu->fixed_100 = 0b100;
    lbu->rs1 = base;
    lbu->rd = dest;
    lbu->offset = offset;
}
static void asm_sb(struct out *out, u8 base, u8 src, u16 offset) {
    struct sb {
        u8 tag : 7;
        u8 offset_lower_5 : 5;
        u8 fixed_000 : 3;
        u8 rs1 : 5;
        u8 rs2 : 5;
        u8 offset_hi_7 : 7;
    } __attribute__((packed)) *sbu = out_resv(out, 4);

    sbu->fixed_000 = 0b000;
    sbu->tag = 0b100011;
    sbu->offset_hi_7 = (offset >> 5) & mask(7);
    sbu->offset_lower_5 = offset & mask(5);
    sbu->rs1 = base;
    sbu->rs2 = src;
}

static void asm_addi(struct out *out, u8 dest, u8 a, i16 imm) {
    struct addi {
        u8 tag : 7;
        u8 rd : 5;
        u8 fixed_000 : 3;
        u8 rs1 : 5;
        u16 imm : 12;
    } __attribute__((packed)) *addi = out_resv(out, 4);

    addi->tag = 0b0010011;
    addi->fixed_000 = 0b000;

    addi->rd = dest;
    addi->rs1 = a;
    addi->imm = *(u16 *)&imm;
}

static void asm_ecall(struct out *out) {
    // ecall is all zeroes except for the tag.
    *(u32 *)out_resv(out, 4) = 0x00000073;
}
