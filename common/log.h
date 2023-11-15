#pragma once
#include <stdio.h>

#define _S(x) #x
#define S(x) _S(x)

#define warn(msg, ...)                                                         \
    fprintf(stderr, "\x1b[1m" __FILE_NAME__ "\x1b[m:" S(                            \
                        __LINE__) ":\twarn: " msg __VA_OPT__(, ) __VA_ARGS__)
#define error(msg, ...)                                                        \
    fprintf(stderr, "\x1b[1m" __FILE_NAME__ "\x1b[m:" S(                            \
                        __LINE__) ":\terror: " msg __VA_OPT__(, ) __VA_ARGS__)
#define log(msg, ...)                                                          \
    fprintf(stderr, "\x1b[1m" __FILE_NAME__ "\x1b[m:" S(                       \
                        __LINE__) ":\tinfo: " msg __VA_OPT__(, ) __VA_ARGS__)
