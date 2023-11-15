#pragma once
#include <stdio.h>

#define warn(msg, ...) fprintf(stderr, "(warn) " msg __VA_OPT__(, ) __VA_ARGS__)
#define error(msg, ...)                                                        \
    fprintf(stderr, "(error) " msg __VA_OPT__(, ) __VA_ARGS__)
#define log(msg, ...) fprintf(stderr, "(info) " msg __VA_OPT__(, ) __VA_ARGS__)
