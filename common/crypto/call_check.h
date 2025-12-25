#include <utils/defs.h>
#include <utils/exception.h>

void __ssl_err(const char *descr);

inline void _ssl_call_check(const char *descr, bool ret_val) {
    if (unlikely(!ret_val)) {
        __ssl_err(descr);
    }
}

#define scall(descr, cmd) _ssl_call_check(__FILE__ ":" TOSTRING(__LINE__) " " #cmd ": " descr, !!(cmd))
