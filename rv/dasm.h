#pragma once

#include "../common/types.h"
#include "insn.h"
#include <stdio.h>

extern char const *opcode_names[];

void dasm(FILE *out, u32 raw, u32 insn_offset);

// vim:ft=c
