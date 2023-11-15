cmake_minimum_required(VERSION 3.25)

project(sam C)

# Avoid inlining of static functions when debugging.
add_compile_options(-g -fno-optimize-sibling-calls)
#add_link_options(-fsanitize=address)
add_executable(sam
        sam.c)

add_executable(cpu 
    cpu.c
    loader.c
    loader.h
    interpret.c
    interpret.h
    emu/dasm.c
    emu/insn.h)

add_executable(bfc 
    bfc.c
    bfc/out.c)
