#pragma once

#include <string>
#include <exception>


class SysError : public std::exception {
    std::string descr;
    public:
    SysError(std::string descr) : descr(descr) {}
    const char *what() const noexcept override {
        return descr.c_str();
    }
};

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
    SSLError(const char * descr, long err);
    SSLError(const char * descr);
    const char *what() const throw ();
    std::string describe();
};
