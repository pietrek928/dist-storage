#pragma once

#include <cstdlib>


template<class Tobj, void(*free_func)(Tobj*)>
class guard_ptr {
    public:

    Tobj *ptr = NULL;

    guard_ptr(Tobj *ptr) : ptr(ptr) {}
    guard_ptr(void *ptr) : ptr((Tobj *)ptr) {}
    guard_ptr() {}

    guard_ptr(const guard_ptr&) = delete;
    guard_ptr& operator=(const guard_ptr&) = delete;

    guard_ptr(guard_ptr&& other) {
        ptr = other.ptr;
        other.ptr = NULL;
    }

    guard_ptr& operator=(guard_ptr&& other) {
        if (ptr) {
            free_func(ptr);
        }
        ptr = other.ptr;
        other.ptr = NULL;
        return *this;
    }

    operator Tobj*() const {
        return ptr;
    }

    operator void*() const {
        return ptr;
    }

    operator bool() const {
        return ptr != NULL;
    }

    bool is_null() const {
        return ptr == NULL;
    }

    Tobj* get() const {
        return ptr;
    }

    Tobj *handle() {
        auto r = ptr;
        ptr = NULL;
        return r;
    }

    ~guard_ptr() {
        if (ptr) {
            free_func(ptr);
        }
    }
};
