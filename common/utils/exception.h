#pragma once

#include <string>
#include <exception>

#include <openssl/err.h>

class ConnectionError : public std::exception {
    std::string descr;
    public:
    ConnectionError(std::string descr) : descr(descr) {}
    const char *what() const noexcept override {
        return descr.c_str();
    }
};

class IdentityError : public std::exception {
    std::string descr;

public:
    IdentityError(std::string descr) : descr(descr) {}

    const char *what() const throw () {
        return descr.c_str();
    }
};

class IntegrityError : public std::exception {
    std::string descr;

public:
    IntegrityError(std::string descr) : descr(descr) {}

    const char *what() const throw () {
        return descr.c_str();
    }
};

class SSLError : public std::exception {
    const char *descr;
    long err;

public:
    SSLError(const char * descr, long err) : descr(descr), err(err) {}
    SSLError(const char * descr) : descr(descr) {
        err = ERR_get_error();
    }

    const char *what() const throw () {
        return descr;
    }

    std::string describe() {
        char buf[256];
        ERR_error_string_n(err, buf, sizeof(buf));
        return std::string(descr) + ": " +  std::string(buf);
    }
};
