#pragma once

#include <unistd.h> // For close()

#include "defs.h"
#include "sys/err.h"


class unique_fd {
    socket_t fd = -1;
public:
    void reset() {
        if (fd != -1) {
            if (close(fd) == -1) {
              show_sys_error("close file descriptor");
            }
            fd = -1;
        }
    }

    bool valid() const {
        return fd != -1;
    }

    unique_fd() {}
    unique_fd(socket_t fd) : fd(fd) {}
    unique_fd(const unique_fd&) = delete;
    unique_fd& operator=(const unique_fd&) = delete;
    unique_fd(unique_fd&& other) : fd(other.fd) {other.fd = -1;}
    unique_fd& operator=(unique_fd&& other) {
        if (this != &other) {
            reset();
            fd = other.fd;
            other.fd = -1;
        }
        return *this;
    }

    operator int() const {
        return fd;
    }
    operator bool() const {
        return valid();
    }

    socket_t handle() {
        auto r = fd;
        fd = -1;
        return r;
    }

    ~unique_fd() {reset();}
};