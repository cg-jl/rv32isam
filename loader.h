
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef __clang__
#define _Nonnull
#endif

struct loaded_exe {
    void *_Nonnull mem;
    size_t mem_count;
    uint64_t entrypoint;
};

int loader_read_elf(int fd, struct loaded_exe *_Nonnull exe);

int loader_read_raw(int fd, struct loaded_exe *_Nonnull exe);

void loader_destroy_exe(struct loaded_exe *_Nonnull exe);

// vim:sw=4:ft=c
