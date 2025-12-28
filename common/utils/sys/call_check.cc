#include "sys_call_check.h"

#include <string>
#include <cstring>

void __sys_err(const char *descr, const char * cmd, int ret_val) {
    throw SysError(
        ((std::string)cmd) + ": " + descr + " returned "
        + std::to_string(ret_val) + ", errno: " + strerror(errno)
    );
}
