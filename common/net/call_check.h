#pragma once

#include <utils/defs.h>
#include <utils/exception.h>


void __conn_err(const char *descr, const char * cmd, int ret_val);

inline void _ccall(const char *descr, const char *cmd, int ret_val) {
    if (unlikely(ret_val < 0)) {
        __conn_err(descr, cmd, ret_val);
    }
}

#define ccall(descr, cmd) _ccall(descr, __FILE__ ":" TOSTRING(__LINE__) " " #cmd, cmd)
