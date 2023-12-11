#include "common/log.h"
#include "interpret.h"
#include "loader.h"
#include "rv/insn.h"
#include <assert.h>
#include <elf.h>
#include <elfutils/elf-knowledge.h>
#include <errno.h>
#include <fcntl.h>
#include <libelf.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

struct cli_options {
    // The file name to open. Uses system-given memory from argument list.
    char *input_file;
    enum input_mode { mode_raw, mode_elf } mode;
    size_t empend_segment_size;
    bool wants_help;
};

static bool parse_options(size_t argc, char **argv, struct cli_options *opts);

static char const USAGE[] =
    "usage: %s <file> [OPTIONS]\n"
    "OPTIONS:\n"
    "\t--help\t\t\tShow this message and exit.\n\n"
    "\t--mode MODE\t\tRead the input in format MODE.\n"
    "\t\t\t\tMODE can be either raw (just the instructions) or 'elf' \n"
    "\t\t\t\t(ELF file containing size of data segment).\n"
    "\t\t\t\tWhen using raw, use --empend-segment to allocate memory for\n"
    "\t\t\t\twanted data.\n\n"
    "\t--empend-segment SIZE\tEmpend a read/write (not executable) segment at\n"
    "\t\t\t\tthe start of the image.\n";

int main(int argc, char **argv) {

    // assume program name is argv[0]
    if (argc == 0)
        return 0;
    struct cli_options opts;

    if (!parse_options(argc - 1, &argv[1], &opts)) {
        printf(USAGE, argv[0]);
        return 1;
    }
    if (opts.wants_help) {
        printf(USAGE, argv[0]);
        return 0;
    }

    if (opts.empend_segment_size != 0 && opts.mode == mode_elf) {
        fprintf(stderr, "If a data segment is required, please use the ELF "
                        "image to specify segment sizes and relocations\n");
        return 1;
    }

    printf("Chosen file is '%s'\n", opts.input_file);

    struct loaded_exe exe;

    int fd = open(opts.input_file, O_RDONLY);
    if (fd == -1) {
        perror("Could not open source file");
        return 1;
    }

    int code = 0;

    switch (opts.mode) {
    case mode_raw:
        code = loader_read_raw(fd, opts.empend_segment_size, &exe);
        break;
    case mode_elf:
        code = loader_read_elf(fd, &exe);
        break;
    }

    if (code != 0) {
        return code;
    }

    interpret(exe.mem, exe.entrypoint);

    loader_destroy_exe(&exe);

    close(fd);

    return 0;
}

// FIXME: maybe this shouldn't handle the errors itself...

static bool parse_options(size_t argc, char **argv, struct cli_options *opts) {
    // defaults.
    opts->empend_segment_size = 0;
    opts->mode = mode_elf;

    // ensure we can detect that we didn't find it.
    opts->input_file = NULL;

    bool res = true;

    // look for flags, removing them from
    for (size_t i = 0; i < argc; ++i) {
        char *arg = argv[i];

        if (*arg != '-') {

            if (opts->input_file != NULL) {
                fprintf(stderr, "Extraneous positional argument: '%s'\n", arg);
                res = false;
            }

            printf("Found input file: '%s'\n", arg);

            opts->input_file = arg;
            continue;
        }

        if (strncmp(arg, "--mode", sizeof("--mode")) == 0) {
            ++i;
            if (i == argc) {
                fprintf(stderr, "--mode flag requires a mode after it.");
                res = false;
                continue;
            }
            char const *mode = argv[i];
            if (strncmp(mode, "raw", sizeof("raw")) == 0) {
                opts->mode = mode_raw;
            } else if (strncmp(mode, "elf", sizeof("elf")) == 0) {
                opts->mode = mode_elf;
            } else {
                fprintf(stderr, "unknown mode: '%s'\n", mode);
                res = false;
            }
        } else if (strncmp(arg, "--empend-segment",
                           sizeof("--empend-segment")) == 0) {
            ++i;
            if (i == argc) {
                fprintf(stderr,
                        "--empend-segment flag requires a SIZE after it.");
                res = false;
                continue;
            }
            char const *size = argv[i];
            char *endptr;
            size_t addend = strtoull(size, &endptr, 0);
            if (addend == ULONG_MAX) {
                res = false;
                fprintf(stderr,
                        "Could not parse size for --empend-segment: %s\n",
                        strerror(errno));
            }

            opts->empend_segment_size += addend;
        } else if (strncmp(arg, "--help", sizeof("--help")) == 0) {
            opts->wants_help = true;
            return true;
        } else {
            res = false;
            fprintf(stderr, "unrecognised flag: '%s'\n", arg);
        }
    }

    if (opts->input_file == NULL) {
        res = false;
        fprintf(stderr, "Missing input file\n");
    }

    return res;
}
