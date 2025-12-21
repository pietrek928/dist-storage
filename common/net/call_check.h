#pragma once

#include <string>

#include <exception.h>

#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)

void __conn_err(const char *descr, const char * cmd, int ret_val);

inline void _ccall(const char *descr, const char *cmd, int ret_val) {
    if (unlikely(ret_val < 0)) {
        __conn_err(descr, cmd, ret_val);
    }
}

#define ccall(descr, cmd) _ccall(descr, __AT__ " " #cmd, cmd)
