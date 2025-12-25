#include "exception.h"

#include <openssl/err.h>

SSLError::SSLError(const char * descr, long err) : descr(descr), err(err) {}
SSLError::SSLError(const char * descr) : descr(descr) {
    err = ERR_get_error();
}

const char *SSLError::what() const throw () {
    return descr;
}

std::string SSLError::describe() {
    char buf[256];
    ERR_error_string_n(err, buf, sizeof(buf));
    return std::string(descr) + ": " +  std::string(buf);
}
