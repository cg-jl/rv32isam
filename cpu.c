#include "interpret.h"
#include "loader.h"
#include "rv/insn.h"
#include <assert.h>
#include <elf.h>
#include <elfutils/elf-knowledge.h>
#include <fcntl.h>
#include <libelf.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

struct cli_options {
    // The file name to open. Uses system-given memory from argument list.
    char *input_file;
};

static bool parse_options(size_t argc, char **argv, struct cli_options *opts);

static char const USAGE[] = "usage: %s <file>\n";

int main(int argc, char **argv) {

    // assume program name is argv[0]
    if (argc == 0)
        return 0;
    struct cli_options opts;

    if (!parse_options(argc - 1, &argv[1], &opts)) {
        printf(USAGE, argv[0]);
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
    if ((code = loader_read_elf(fd, &exe))) {
        return code;
    }

    interpret(exe.mem, exe.entrypoint);

    loader_destroy_exe(&exe);

    close(fd);

    return 0;
}

// FIXME: maybe this shouldn't handle the errors itself...

static bool parse_options(size_t argc, char **argv, struct cli_options *opts) {
    if (argc == 1) {
        opts->input_file = argv[0];
        return true;
    } else {
        fprintf(stderr, "Missing argument: <raw file>\n");
        return false;
    }
};
