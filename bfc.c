// q&d brainfuck -> rv32i compiler.
// The code has a tape of 3K to work with.
#include "bfc/out.h"
#include "common/types.h"
#include "rv/insn.h"
#include <elf.h>
#include <fcntl.h>
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
static void asm_addi(struct out *out, u8 dest, u8 a, i16 imm_12);
static void asm_lui(struct out *out, u8 dest, i32 imm_20);
static void asm_lbu(struct out *out, u8 base, u8 dest, u16 offset);
static void asm_sb(struct out *out, u8 base, u8 src, u16 offset);
static void asm_or(struct out *out, u8 dest, u8 a, u8 b);

struct rospan {
    void const *ptr;
    u32 len;
};

static void compile_stdin(struct out *insns);
static void emit_elf(struct rospan insns);

static u32 strtab_add(struct out *strtab, char const *name);

int main(void) {

    struct out insns = {0};

    compile_stdin(&insns);
    emit_elf((struct rospan){.ptr = insns.bytes, .len = insns.len});

    out_destroy(&insns);
}

static void compile_stdin(struct out *insns) {
    // set stdin to unbuffered so getchar() doesn't go on lines.
    setvbuf(stdin, NULL, _IONBF, 0);

    struct out loop_begin_stack = {0};

    // Load the code address for the data segment, which has 3KiB
    // FIXME: This should be some sort of `lui` + `addi` mix, since addi is
    // restricted to 12-bit immediates.
    asm_lui(insns, rv_s1, rv_zero);
    asm_addi(insns, rv_s1, rv_zero, 0);

    struct loop_info {
        u32 check_offset;
        u32 beq_offset;
    };

#define push_loop(chk, beq)                                                    \
    {                                                                          \
        struct loop_info *info = out_resv(&loop_begin_stack, sizeof(*info));   \
        info->check_offset = chk;                                              \
        info->beq_offset = beq;                                                \
    }
#define pop_loop(loc)                                                          \
    {                                                                          \
        loop_begin_stack.len -= sizeof(struct loop_info);                      \
        loc = *(struct loop_info *)(loop_begin_stack.bytes +                   \
                                    loop_begin_stack.len);                     \
    }
#define load_cell(dest) asm_lbu(insns, rv_s1, dest, 0)
#define store_cell(src) asm_sb(insns, rv_s1, src, 0)

    {
        char c;
        while ((c = getchar()) != EOF) {
            switch (c) {
            case '[': {
                u32 check_offset = insns->len;
                // if [cell] != 0 goto loop_end (will be patched when finding
                // ']')
                load_cell(rv_t0);
                push_loop(check_offset, insns->len);
                asm_empty_beq(insns, rv_t0, rv_zero);
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
                    jal->rd = rv_zero;
                    // XXX: I don't know if this will interpret it correctly.
                    jal->offset = -dist_from_chk;
                }

                break;
            }
            case '+':
                load_cell(rv_t0);
                asm_addi(insns, rv_t0, rv_t0, 1);
                store_cell(rv_t0);
                break;
            case '-':
                load_cell(rv_t0);
                asm_addi(insns, rv_t0, rv_t0, -1);
                store_cell(rv_t0);
                break;
            case '.':
                // write(1, rv_s1, 1);
                asm_addi(insns, rv_a7, rv_zero, 64);
                asm_addi(insns, rv_a0, rv_zero, 1);
                asm_or(insns, rv_a0, rv_zero, rv_s1);
                asm_addi(insns, rv_a2, rv_zero, 1);
                asm_ecall(insns);
                // we don't care about the return value.
                break;
            case ',':
                // read(0, rv_s1, 1);
                asm_addi(insns, rv_a7, rv_zero, 63);
                asm_addi(insns, rv_a0, rv_zero, 1);
                asm_or(insns, rv_a0, rv_zero, rv_s1);
                asm_addi(insns, rv_a2, rv_zero, 1);
                asm_ecall(insns);
                // we don't care about the return value.
                break;
            case '>':
                asm_addi(insns, rv_s1, rv_s1, 1);
                break;
            case '<':
                asm_addi(insns, rv_s1, rv_s1, -1);
                break;
            }
        }
    }

    // exit(0)
    asm_addi(insns, rv_a7, rv_zero, 93);
    asm_addi(insns, rv_a0, rv_zero, 0);
    asm_ecall(insns);

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

// elf emitting
static void emit_elf(struct rospan insns) {
    struct out elf = {0};
    u32 page_size = sysconf(_SC_PAGESIZE);

    out_resv_index(&elf, sizeof(Elf32_Ehdr));

#define ehdr ((Elf32_Ehdr *)elf.bytes)

    u32 segment_count = 0;
    u32 data_segm = segment_count++;
    u32 text_segm = segment_count++;
    u32 phoff = out_resv_index(&elf, sizeof(Elf32_Phdr) * segment_count);

    // segments & sections defined as macros since `elf.bytes` might
    // change, hence the binding isn't recomputed.
#define segments ((Elf32_Phdr *)(elf.bytes + phoff))

    u32 section_count = 0;
    u32 null_shdr = section_count++;
    u32 data_shdr = section_count++;
    u32 text_shdr = section_count++;
    u32 rela_shdr = section_count++;
    u32 symtab_shdr = section_count++;
    u32 strtab_shdr = section_count++;
    u32 shstrtab_shdr = section_count++;
    u32 shoff = out_resv_index(&elf, sizeof(Elf32_Shdr) * section_count);

#define sections ((Elf32_Shdr *)(elf.bytes + shoff))
    // Now that all headers are reserved, we can assign offsets directly
    // when copying data to the finel file.

    // segment layout: <data> <instructions>
    u32 data_begin_virt = 0;
    u32 insns_begin_virt = data_begin_virt + 3 * 1024ul;

    u32 insns_begin_elf = out_write_index(&elf, insns.ptr, insns.len);

    struct out shstrtab = {0};
    {
        {
            sections[null_shdr] = (Elf32_Shdr){0};
            sections[null_shdr].sh_name = strtab_add(&shstrtab, "");
        }
        // .text: segment & section
        {
            sections[text_shdr].sh_name = strtab_add(&shstrtab, ".text");
            sections[text_shdr].sh_type = SHT_PROGBITS;
            sections[text_shdr].sh_flags = SHF_ALLOC | SHF_EXECINSTR;
            sections[text_shdr].sh_addr = insns_begin_virt;
            sections[text_shdr].sh_offset = insns_begin_elf;
            sections[text_shdr].sh_size = insns.len;
            sections[text_shdr].sh_link = sections[text_shdr].sh_info = 0;
            sections[text_shdr].sh_addralign = page_size;
            sections[text_shdr].sh_entsize = 0;

            segments[text_segm].p_type = PT_LOAD;
            segments[text_segm].p_offset = insns_begin_elf;
            segments[text_segm].p_vaddr = insns_begin_virt;
            segments[text_segm].p_paddr = segments[text_segm].p_vaddr;
            segments[text_segm].p_filesz = insns.len;
            segments[text_segm].p_memsz = insns.len;
            segments[text_segm].p_flags = PF_R | PF_X;
            segments[text_segm].p_align = 4;
        }

        // .data: section & segment
        {
            sections[data_shdr].sh_name = strtab_add(&shstrtab, ".data");
            sections[data_shdr].sh_type = SHT_NOBITS;
            sections[data_shdr].sh_flags = SHF_ALLOC | SHF_WRITE;
            sections[data_shdr].sh_addr = data_begin_virt;
            sections[data_shdr].sh_offset = insns_begin_elf;
            sections[data_shdr].sh_size = 3 * 1024;
            sections[data_shdr].sh_link = sections[data_shdr].sh_info = 0;
            sections[data_shdr].sh_addralign = page_size;
            sections[data_shdr].sh_entsize = 0;

            segments[data_segm].p_type = PT_LOAD;
            // It's not in the file, but it would come here, after the
            // instructions.
            segments[data_segm].p_offset = insns_begin_elf + insns.len;
            segments[data_segm].p_vaddr = data_begin_virt;
            segments[data_segm].p_paddr = segments[data_segm].p_vaddr;
            segments[data_segm].p_memsz = 3 * 1024;
            segments[data_segm].p_filesz = 0;
            segments[data_segm].p_flags = PF_R | PF_W;
            segments[data_segm].p_align = page_size;
        }
#undef segments

        // Emit relocation for the stack start
        struct out strtab = {0};
        {
            u32 sym_count = 0;

            // NOTE: The order of symbols matters:
            // - local
            // - weak
            // - global

            struct out symtab = {0};
#define resv_sym() (out_resv(&symtab, sizeof(Elf32_Sym)), sym_count++)
#define syms ((Elf32_Sym *)symtab.bytes)
            {

                // create symbol for use in the relocations for the data
                // segment.
                u32 data_sym = resv_sym();
                u32 data_sym_ndx = sym_count - 1;
                syms[data_sym].st_name = strtab_add(&strtab, "_bf_data");
                syms[data_sym].st_value = 0;
                syms[data_sym].st_size = 3 * 1024;
                syms[data_sym].st_info = ELF32_ST_INFO(STB_LOCAL, STT_OBJECT);
                syms[data_sym].st_other = STV_DEFAULT;
                syms[data_sym].st_shndx = data_shdr;

                u32 rela_size = 2 * sizeof(Elf32_Rela);
                u32 rela_offt = out_resv_index(&elf, rela_size);
                {
                    Elf32_Rela *lui = elf.bytes + rela_offt;
                    Elf32_Rela *addi = lui + 1;
                    // lui rv_s1, <data address>.hi20
                    lui->r_offset = 0;
                    lui->r_info = ELF32_R_INFO(data_sym_ndx, R_RISCV_HI20);
                    lui->r_addend = 0;

                    // addi rv_s1, <data address>.lo12
                    addi->r_offset = 4;
                    addi->r_info = ELF32_R_INFO(data_sym_ndx, R_RISCV_HI20);
                    addi->r_addend = 0;
                }

                // finish relocation table
                sections[rela_shdr].sh_name =
                    strtab_add(&shstrtab, ".rela.text");
                sections[rela_shdr].sh_type = SHT_RELA;
                sections[rela_shdr].sh_flags = 0;
                sections[rela_shdr].sh_addr = 0;
                sections[rela_shdr].sh_offset = rela_offt;
                sections[rela_shdr].sh_size = rela_size;
                sections[rela_shdr].sh_link = symtab_shdr;
                sections[rela_shdr].sh_info = text_shdr;
                sections[rela_shdr].sh_addralign = 0;
                sections[rela_shdr].sh_entsize = sizeof(Elf32_Rela);
            }

            // entrypoint symbol so that it can be disassembled
            u32 entry_sym = resv_sym();
            syms[entry_sym].st_name = strtab_add(&strtab, "_bf_entry");
            syms[entry_sym].st_value = 0;
            syms[entry_sym].st_size = insns.len;
            syms[entry_sym].st_info = ELF32_ST_INFO(STB_LOCAL, STT_FUNC);
            syms[entry_sym].st_other = STV_DEFAULT;
            syms[entry_sym].st_shndx = text_shdr;

#undef syms
#undef resv_sym

            // finish symbol table
            u32 symtab_elf_begin =
                out_write_index(&elf, symtab.bytes, symtab.len);
            sections[symtab_shdr].sh_name = strtab_add(&shstrtab, ".symtab");
            sections[symtab_shdr].sh_type = SHT_SYMTAB;
            sections[symtab_shdr].sh_flags = 0;
            sections[symtab_shdr].sh_addr = 0;
            sections[symtab_shdr].sh_offset = symtab_elf_begin;
            sections[symtab_shdr].sh_size = symtab.len;
            sections[symtab_shdr].sh_addralign = 1;
            sections[symtab_shdr].sh_link = strtab_shdr;
            // All of our symbols are local.
            sections[symtab_shdr].sh_info = sym_count;
            sections[symtab_shdr].sh_entsize = sizeof(Elf32_Sym);
            out_destroy(&symtab);
        }

        // finish string table, which was only used for the symbol table.
        u32 strtab_elf_begin = out_write_index(&elf, strtab.bytes, strtab.len);
        sections[strtab_shdr].sh_name = strtab_add(&shstrtab, ".strtab");
        sections[strtab_shdr].sh_type = SHT_STRTAB;
        sections[strtab_shdr].sh_flags = SHF_STRINGS;
        sections[strtab_shdr].sh_addr = 0;
        sections[strtab_shdr].sh_offset = strtab_elf_begin;
        sections[strtab_shdr].sh_size = strtab.len;
        sections[strtab_shdr].sh_addralign = 1;
        sections[strtab_shdr].sh_link = 0;
        sections[strtab_shdr].sh_info = 0;
        sections[strtab_shdr].sh_entsize = 1;

        out_destroy(&strtab);
    }

    // finish section header string table.
    sections[shstrtab_shdr].sh_name = strtab_add(&shstrtab, ".shstrtab");
    sections[shstrtab_shdr].sh_type = SHT_STRTAB;
    sections[shstrtab_shdr].sh_flags = SHF_STRINGS;
    sections[shstrtab_shdr].sh_addr = 0;
    sections[shstrtab_shdr].sh_offset =
        out_write_index(&elf, shstrtab.bytes, shstrtab.len);
    sections[shstrtab_shdr].sh_addralign = 1;
    sections[shstrtab_shdr].sh_link = 0;
    sections[shstrtab_shdr].sh_info = 0;
    sections[shstrtab_shdr].sh_size = shstrtab.len;
    sections[shstrtab_shdr].sh_entsize = 1;
    out_destroy(&shstrtab);

#undef sections

    // finish ELF file
    memcpy(ehdr->e_ident + 1, "ELF", 3);
    ehdr->e_ident[EI_MAG0] = 0x7f;

    ehdr->e_ident[EI_CLASS] = ELFCLASS32;
    ehdr->e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr->e_ident[EI_VERSION] = EV_CURRENT;
    ehdr->e_ident[EI_OSABI] = ELFOSABI_SYSV;
    ehdr->e_ident[EI_ABIVERSION] = 0;
    memset(ehdr->e_ident + EI_PAD, 0, EI_NIDENT - EI_PAD);
    ehdr->e_type = ET_EXEC;
    ehdr->e_machine = EM_RISCV;
    ehdr->e_version = EV_CURRENT;
    ehdr->e_flags = 0;
    ehdr->e_phnum = segment_count;
    ehdr->e_phoff = phoff;
    ehdr->e_phentsize = sizeof(Elf32_Phdr);
    ehdr->e_shnum = section_count;
    ehdr->e_shoff = shoff;
    ehdr->e_shentsize = sizeof(Elf32_Shdr);
    ehdr->e_ehsize = sizeof(*ehdr);
    ehdr->e_shstrndx = shstrtab_shdr;
    ehdr->e_entry = insns_begin_virt;

    int outfd = open("a.out", O_RDWR | O_TRUNC | O_CREAT, 0755);

    if (outfd == -1) {
        perror("open");
        goto clean_out;
    }

    if (write(outfd, elf.bytes, elf.len) != elf.len) {
        perror("write");
        goto clean_file;
    }

clean_file:
    close(outfd);

clean_out:
    out_destroy(&elf);
}

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

static void asm_lui(struct out *out, u8 dest, i32 imm_20) {
    struct lui {
        u8 tag : 7;
        u8 rd : 5;
        i32 imm : 20;
    } __attribute__((packed)) *lui = out_resv(out, 4);
    lui->tag = 0b0110111;
    lui->rd = dest;
    lui->imm = imm_20;
}

static void asm_ecall(struct out *out) {
    // ecall is all rv_zeroes except for the tag.
    *(u32 *)out_resv(out, 4) = 0x00000073;
}
