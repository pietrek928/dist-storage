#include "helpers.h"

#include <cstring>

void __conn_err(const char *descr, const char * cmd, int ret_val) {
    throw ConnectionError(
        ((std::string)cmd) + ": " + descr + " returned "
        + std::to_string(ret_val) + ", errno: " + strerror(errno)
    );
}
