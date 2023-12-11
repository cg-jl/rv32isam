#include "loader.h"
#include "common/bit_math.h"
#include "common/log.h"
#include "common/types.h"
#include <assert.h>
#include <elf.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int mmap_file(int fd, void **file, size_t *filesz);

int loader_read_raw(int fd, u32 data_segment_count,
                    struct loaded_exe *_Nonnull exe) {

    int code;
    if ((code = mmap_file(fd, &exe->mem, &exe->mem_count)) != 0)
        return code;

    exe->entrypoint = 0;
    if (data_segment_count > 0) {
        u32 page_size = sysconf(_SC_PAGESIZE);

        u32 aligned_count = align_upwards(data_segment_count, page_size);

        void *wanted_start_addr = exe->mem - aligned_count;

        log("aligned count: %u\n", aligned_count);
        log("expected start address: %p\n", wanted_start_addr);

        if (mmap(wanted_start_addr, aligned_count, PROT_WRITE | PROT_READ,
                 MAP_ANONYMOUS | MAP_FIXED_NOREPLACE | MAP_PRIVATE, -1,
                 0) != wanted_start_addr) {
            error("Could not create data segment: %s\n", strerror(errno));
            return errno;
        }
        exe->mem = wanted_start_addr;
        exe->entrypoint = aligned_count;
    }

    return 0;
}

void loader_destroy_exe(struct loaded_exe *_Nonnull exe) {
    munmap(exe->mem, exe->mem_count);
}

struct page_descr {
    struct page_descr *next;
    size_t offset_from_mmap;
    uint8_t flags;
    size_t size;
};

struct page_descr_list {
    struct page_descr *tail;
    struct page_descr *head;
};

struct loadable_segment {
    struct loadable_segment *next;
    size_t mem_offset;
    Elf32_Phdr const *phdr;
};

// To mantain order, nodes are appended to the list's end, but we can start
// looking at the list from the start.
struct loadable_segments_list {
    struct loadable_segment *tail;
    struct loadable_segment *head;
};

// TODO: use dynamic arrays for these, instead of linked lists.

// Appends a segment to the list. The pointer returned is not guaranteed to be
// stable after calling add_segment() again.
static struct loadable_segment *
add_segment(struct loadable_segments_list *list);

static void destroy_segment_list(struct loadable_segments_list *list);
// size is set when the
static struct page_descr *add_page(struct page_descr_list *list, size_t offset,
                                   uint8_t flags);
static void set_page_end(struct page_descr *descr, size_t end_offset);
static void destroy_page_list(struct page_descr_list *list);

static bool find_offset_for_section(Elf32_Shdr const *section,
                                    struct loadable_segments_list list,
                                    u32 *result);

int loader_read_elf(int fd, struct loaded_exe *_Nonnull exe) {
    log("Begin loading image fd = %u\n", fd);
    union {
        void *begin;
        Elf32_Ehdr *elf;
        unsigned char (*elf_ident)[16];
    } as;
    size_t file_size;

    bool code = 0;

    if ((code = mmap_file(fd, &as.begin, &file_size)) != 0) {
        goto clean_none;
    }

    static char const expected_ident[] = {
        // EI_MAG*
        0x7f, 'E', 'L', 'F',
        // EI_CLASS == ELFCLASS32
        ELFCLASS32,
        // EI_DATA == ELFDATA2LSB
        ELFDATA2LSB,
        // EI_VERSION == EV_CURRENT
        EV_CURRENT,
        // EI_OSABI == ELFOSABI_SYSV (linux)
        ELFOSABI_SYSV,
        // EI_ABIVERSION == 0
        0,
        // the rest is pad, which is ignored.

    };

    if (memcmp(as.elf_ident, expected_ident, sizeof(expected_ident)) != 0) {
        fputs("(error) Bad ELF magic: Should be 32-bit LSB ELF file for SysV "
              "v0.\n",
              stderr);
        code = ENOEXEC;
        goto clean_mapped;
    }

    // Now we're safe to read it as an ELF file.
    // Verify e_machine
    if (as.elf->e_machine != EM_RISCV) {
        fputs("(error) Machine should be RISCV\n", stderr);
        code = ENOEXEC;
        goto clean_mapped;
    }

    if (!as.elf->e_entry) {
        fputs("(error) Executable MUST have an associated entrypoint!\n",
              stderr);
        code = ENOEXEC;
    }

    // TODO: If the ELF file has RVC flag set, then alignment can be 16-bit.

    struct loadable_segments_list memory_segms = {0};

    bool found_any_exec_segment = false;
    Elf32_Phdr const *segments = as.begin + as.elf->e_phoff;

    if (!__builtin_expect(as.elf->e_phentsize == sizeof(Elf32_Phdr), 1)) {
        if (as.elf->e_phentsize == 0) {
            fputs("(error): This file doesn't have any executable code!\n",
                  stderr);
            code = ENOEXEC;
            goto clean_mapped;
        }
    }

    for (Elf32_Half i = 0; i < as.elf->e_phnum; ++i) {
        if (segments[i].p_type == PT_LOAD) {
            add_segment(&memory_segms)->phdr = &segments[i];
            found_any_exec_segment |=
                (segments[i].p_flags & (PF_X | PF_R)) == (PF_X | PF_R);
        }
        if (segments[i].p_type == PT_INTERP) {
            fputs("(error): Custom interpreter not supported\n", stderr);
            code = ENOEXEC;
            goto clean_segments_list;
        }
    }

    if (!found_any_exec_segment) {
        fputs("(error): This file dosen't have any executable code!\n", stderr);
        goto clean_segments_list;
    }

    // Since the segments are stored in order, we know that the first loadable
    // segment has the lowest virtual address.
    uint32_t starting_virtual_address = memory_segms.head->phdr->p_vaddr;

    size_t const page_size = sysconf(_SC_PAGESIZE);

    if (__builtin_expect(memory_segms.head->phdr->p_align > page_size, 0)) {
        // The rest of alignments can be satisified by manipulating
        // `total_map_size`.
        // The first alignment imposes a requirement on the `mmap()` call,
        // where we are restricted to `page_size` alignment.
        fputs("(error): Requested alignment for 1st segment bigger than page "
              "size\n",
              stderr);
        code = ENOEXEC;
        goto clean_segments_list;
    }
    size_t next_segment_offset = memory_segms.head->phdr->p_memsz;
    struct loadable_segment *prev_segment = memory_segms.head;
    struct page_descr_list pages = {0};
    struct page_descr *current_page =
        add_page(&pages, 0, memory_segms.head->phdr->p_flags);

    memory_segms.head->mem_offset = 0;

    // Start on the 2nd segment.
    for (struct loadable_segment *segm = memory_segms.head->next; segm != NULL;
         prev_segment = segm, segm = segm->next) {
        // The minimum distance between segment starts
        size_t distance_between_starts =
            segm->phdr->p_vaddr - prev_segment->phdr->p_vaddr;
        size_t distance_end_to_start =
            distance_between_starts - prev_segment->phdr->p_memsz;
        size_t curr_distance = next_segment_offset - prev_segment->mem_offset;

        if (curr_distance < distance_end_to_start) {
            next_segment_offset += distance_end_to_start - curr_distance;
        }
        // align the offset to the segment's alignment request.
        // 1 or 0 means no alignment required.
        // Note that due to how the `align_upwards` works, putting 0 into
        // `align` will make the value 0. The 1 will work OK, but we can just
        // start at 2.
        if (segm->phdr->p_align >= 2) {
            next_segment_offset =
                align_upwards64(next_segment_offset, segm->phdr->p_align);
        }
        // We're forced to make a new page, since we have to mprotect() that
        if (segm->phdr->p_flags != current_page->flags) {
            next_segment_offset =
                align_upwards64(next_segment_offset, page_size);
            set_page_end(current_page, next_segment_offset);
            current_page =
                add_page(&pages, next_segment_offset, segm->phdr->p_flags);
        }

        uint32_t segm_virtual_end = segm->phdr->p_vaddr + segm->phdr->p_memsz;

        // Ensure we patch the entrypoint so that it's correctly based on our
        // offset.
        if (as.elf->e_entry >= segm->phdr->p_vaddr &&
            as.elf->e_entry < segm_virtual_end) {
            exe->entrypoint = as.elf->e_entry - segm->phdr->p_vaddr;
            exe->entrypoint += next_segment_offset;
        }

        segm->mem_offset = next_segment_offset;
        next_segment_offset += segm->phdr->p_memsz;
    }

    set_page_end(current_page, next_segment_offset);

    size_t full_memory_image_size = next_segment_offset;
    void *memory = mmap(NULL, full_memory_image_size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (memory == MAP_FAILED) {
        perror("mmap");
        code = errno;
        goto clean_pages_list;
    }

    // Copy segments to their memory locations
    for (struct loadable_segment *segm = memory_segms.head; segm != NULL;
         segm = segm->next) {

        log("segm filesz  = 0x%x\n", segm->phdr->p_filesz);
        log("segm offset = 0x%lx\n", segm->mem_offset);

        memcpy(memory + segm->mem_offset, as.begin + segm->phdr->p_offset,
               segm->phdr->p_filesz);
        // per the ELF(5) man page: if memsz > filesz then there are (memsz -
        // filesz) zeroes inserted at the end of the segment.
        if (segm->phdr->p_filesz < segm->phdr->p_memsz) {
            memset(memory + segm->mem_offset + segm->phdr->p_filesz, 0,
                   segm->phdr->p_memsz - segm->phdr->p_filesz);
        }
    }

    Elf32_Shdr const *sections = as.begin + as.elf->e_shoff;
    char const *shnames =
        as.elf->e_shoff == 0
            ? NULL
            : as.begin + sections[as.elf->e_shstrndx].sh_offset;
    for (Elf32_Half i = 0; i < as.elf->e_shnum; ++i) {
        Elf32_Shdr const *rela_shdr = &sections[i];
        char const *shname =
            shnames == 0 ? NULL : (shnames + rela_shdr->sh_name);
        if (rela_shdr->sh_type == SHT_RELA) {
            log("Found relocation table '%s'\n", shname);
            Elf32_Shdr const *symtab = &sections[rela_shdr->sh_link];
            Elf32_Sym const *syms = as.begin + symtab->sh_offset;
            char const *sym_names =
                as.begin + sections[symtab->sh_link].sh_offset;
            assert(rela_shdr->sh_entsize == sizeof(Elf32_Rela));

            Elf32_Word rela_count = rela_shdr->sh_size / sizeof(Elf32_Rela);
            log("Have %u relocations\n", rela_count);

            Elf32_Rela const *relatbl = as.begin + rela_shdr->sh_offset;

            u32 patch_base;
            if (!find_offset_for_section(&sections[rela_shdr->sh_info],
                                         memory_segms, &patch_base)) {
                error("Patch location for '%s' is relative to '%s', which "
                      "can't be mapped to the memory image!\n",
                      shnames ? shnames + rela_shdr->sh_name : NULL,
                      shnames ? shnames + sections[rela_shdr->sh_info].sh_name
                              : NULL);
                goto clean_mapped_exe;
            }

            log("Patch base for '%s' = 0x%x\n",
                shnames ? shnames + sections[rela_shdr->sh_info].sh_name : NULL,
                patch_base);

            for (Elf32_Word i = 0; i < rela_count; ++i) {
                Elf32_Rela const *rela = &relatbl[i];

                u32 const sym_ndx = ELF32_R_SYM(rela->r_info);

                u32 const sym_shndx = syms[sym_ndx].st_shndx;

                log("Relocation @  0x%x S =  '%s' (0x%x from '%s') using type "
                    "= "
                    "%u\n",
                    rela->r_offset, sym_names + syms[sym_ndx].st_name,
                    syms[sym_ndx].st_value,
                    sections[sym_shndx].sh_name + shnames,
                    ELF32_R_TYPE(rela->r_info));
                u32 sym_base;
                if (!find_offset_for_section(&sections[sym_shndx], memory_segms,
                                             &sym_base)) {
                    error("Symbol location is relative to '%s', "
                          "which can't be mapped to the memory image!\n",
                          shnames == NULL
                              ? NULL
                              : (sections[sym_shndx].sh_name + shnames));
                    goto clean_mapped_exe;
                }
                u32 s = sym_base + syms[sym_ndx].st_value;
                log("S = 0x%x\n", s);
                switch (ELF32_R_TYPE(rela->r_info)) {
                case R_RISCV_HI20: {
                    u32 result = s + rela->r_addend;

                    u32 *insn_to_patch = memory + patch_base + rela->r_offset;
                    log("Before patch: 0x%08x\n", *insn_to_patch);
                    *insn_to_patch |= result & ~((1ul << 20) - 1);
                    log("After patch: 0x%08x\n", *insn_to_patch);

                }

                break;

                default:
                    error("Unknown relocation %u, refusing to execute\n",
                          ELF32_R_TYPE(rela->r_info));
                    code = ENOEXEC;
                    goto clean_mapped_exe;
                }
            }
        }
    }

    // Set protections
    for (struct page_descr *page = pages.head; page != NULL;
         page = page->next) {

        int prot = 0;

#define hasflag(flag) ((page->flags & flag))
        if (hasflag(PF_R))
            prot |= PROT_READ;
        if (hasflag(PF_W))
            prot |= PROT_WRITE;
        if (hasflag(PF_X))
            prot |= PROT_EXEC;
#undef hasflag

        if (mprotect(memory + page->offset_from_mmap, page->size, prot) != 0) {
            perror("mprotect");
            code = errno;
            goto clean_mapped_exe;
        }
    }

    exe->mem = memory;
    exe->mem_count = full_memory_image_size;

    log("Finished loading image from fd = %u\n", fd);

    goto clean_pages_list;

clean_mapped_exe:
    munmap(memory, full_memory_image_size);

clean_pages_list:
    destroy_page_list(&pages);

clean_segments_list:
    destroy_segment_list(&memory_segms);

clean_mapped:
    munmap(as.begin, file_size);
clean_none:
    return code;
}

// TODO: this *will* be slow for multiple relocations. Cache this with a table
// section index <-> lazy-loaded result (or error)
static bool find_offset_for_section(Elf32_Shdr const *section,
                                    struct loadable_segments_list segms,
                                    u32 *result) {
    for (struct loadable_segment *segm = segms.head; segm != NULL;
         segm = segm->next) {
        u32 virt_start = segm->phdr->p_vaddr;
        u32 virt_end = segm->phdr->p_vaddr + segm->phdr->p_memsz;
        // I'm not taking the section size into account; should I?
        // It doesn't really make sense to have a section outside the segment,
        // since sections have to be fully contained inside segments.
        if (section->sh_addr >= virt_start && section->sh_addr < virt_end) {
            size_t res = segm->mem_offset + (section->sh_addr - virt_start);
            assert(res < UINT32_MAX);
            *result = res;
            return true;
        }
    }
    return false;
}

static struct loadable_segment *
add_segment(struct loadable_segments_list *list) {
    struct loadable_segment *segm = malloc(sizeof(*segm));
    segm->next = NULL;
    if (list->tail) {
        list->tail->next = segm;
        list->tail = segm;
    } else {
        list->head = list->tail = segm;
    }
    return segm;
}

static void destroy_segment_list(struct loadable_segments_list *list) {
    for (struct loadable_segment *segm = list->head; segm != NULL;) {
        struct loadable_segment *next = segm->next;
        free(segm);
        segm = next;
    }
}

static struct page_descr *add_page(struct page_descr_list *list, size_t offset,
                                   uint8_t flags) {
    struct page_descr *page = malloc(sizeof(*page));
    page->offset_from_mmap = offset;
    page->flags = flags;

    page->next = NULL;
    if (list->tail) {
        list->tail->next = page;
        list->tail = page;
    } else {
        list->head = list->tail = page;
    }

    return page;
}

static void set_page_end(struct page_descr *page, size_t end_offset) {
    page->size = end_offset - page->offset_from_mmap;
}

static void destroy_page_list(struct page_descr_list *list) {
    for (struct page_descr *page = list->head; page != NULL;) {
        struct page_descr *next = page->next;
        free(page);
        page = next;
    }
}

static int mmap_file(int fd, void **file, size_t *filesz) {
    struct stat st;
    if (fstat(fd, &st) != 0) {
        perror("cannot stat file");
        return errno;
    }

    void *addr = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        perror("cannot mmap file");
        return errno;
    }

    *file = addr;
    *filesz = st.st_size;
    return 0;
}
