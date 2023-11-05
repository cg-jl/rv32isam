#include "loader.h"
#include <elf.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int mmap_file(int fd, void **file, size_t *filesz);

int loader_read_raw(int fd, struct loaded_exe *_Nonnull exe) {
    int code;
    if ((code = mmap_file(fd, &exe->mem, &exe->mem_count)) != 0)
        return code;
    exe->entrypoint = 0;
    return 0;
}

void loader_destroy_exe(struct loaded_exe *_Nonnull exe) {
    munmap(exe->mem, exe->mem_count);
}

struct something {
    // now you want to work?
};

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

// Aligns `val` to `align` by bumping it to the bigger aligned value.
// If `val` as already aligned by `align`, this algorithm yields `val`.
static uint64_t align_upwards(uint64_t val, uint64_t align) {
    return (val + (align - 1)) & ~(align - 1);
}

int loader_read_elf(int fd, struct loaded_exe *_Nonnull exe) {
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
    // TODO: relocations

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
                align_upwards(next_segment_offset, segm->phdr->p_align);
        }
        // We're forced to make a new page, since we have to mprotect() that
        if (segm->phdr->p_flags != current_page->flags) {
            next_segment_offset = align_upwards(next_segment_offset, page_size);
            set_page_end(current_page, next_segment_offset);
            current_page =
                add_page(&pages, next_segment_offset, segm->phdr->p_flags);
        }
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
         ++segm) {
        memcpy(memory + segm->mem_offset, as.begin + segm->phdr->p_offset,
               segm->phdr->p_filesz);
        // per the ELF(5) man page: if memsz > filesz then there are (memsz -
        // filesz) zeroes inserted at the end of the segment.
        if (segm->phdr->p_filesz < segm->phdr->p_memsz) {
            memset(memory + segm->mem_offset + segm->phdr->p_filesz, 0,
                   segm->phdr->p_memsz - segm->phdr->p_filesz);
        }
    }

    // TODO: relocations

    // Set protections
    for (struct page_descr *page = pages.head; page != NULL;
         page = page->next) {

        int prot = 0;
        uint8_t flags = page->flags & (PF_R | PF_W | PF_X);

#define hasflag(flag) ((flags & flag) == flag)
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
            goto clean_pages_list;
        }
    }

    exe->mem = memory;
    exe->mem_count = full_memory_image_size;
    exe->entrypoint = as.elf->e_entry - starting_virtual_address;

clean_pages_list:
    destroy_page_list(&pages);

clean_segments_list:
    destroy_segment_list(&memory_segms);

clean_mapped:
    munmap(as.begin, file_size);
clean_none:
    return code;
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
    for (struct loadable_segment *segm = list->head; segm != NULL;
         segm = segm->next) {
        free(segm);
    }
}

static struct page_descr *add_page(struct page_descr_list *list, size_t offset,
                                   uint8_t flags) {
    struct page_descr *page = malloc(sizeof(*page));

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
    for (struct page_descr *page = list->head; page != NULL;
         page = page->next) {
        free(page);
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
