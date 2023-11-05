cmake_minimum_required(VERSION 3.25)

project(sam C)

add_executable(sam
        sam.c)

add_executable(cpu 
    cpu.c
    loader.c
    loader.h
    interpret.c
    interpret.h
    emu/insn.h)

add_executable(bfc 
    bfc.c
    bfc/out.c)

# Avoid inlining of static functions when debugging.
target_compile_options(cpu PRIVATE -g -fno-optimize-sibling-calls)
