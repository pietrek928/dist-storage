#pragma once

#include <utils/defs.h>
#include <utils/exception.h>


void __sys_err(const char *descr, const char * cmd, int ret_val);

inline void _sys_call(const char *descr, const char *cmd, int ret_val) {
    if (unlikely(ret_val == -1)) {
        __sys_err(descr, cmd, ret_val);
    }
}

#define sys_call(descr, cmd) _sys_call(descr, __FILE__ ":" TOSTRING(__LINE__) " " #cmd, (cmd))
