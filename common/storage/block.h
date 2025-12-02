#pragma once

#include <cstddef>
#include <stdexcept>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "unique_fd.h"


typedef unsigned char byte;

int get_open_flags(bool readonly, bool create);
int get_prot_flags(bool readonly);
size_t read_data(
    const void *data, size_t elem_size, size_t n,
    void *out_data, size_t offset, size_t data_count
);
size_t write_data(
    const void *in_data, size_t offset, size_t data_count,
    void *data, size_t elem_size, size_t n, bool readonly = false
);
unique_fd init_data_file(const char *fname, bool readonly, size_t size);
unique_fd open_data_file(const char *fname, bool readonly, size_t *size = NULL);
void *mmap_data(int fd, const char *fname, bool readonly, size_t size);

template<class T = byte>
class BlockStorage {
    unique_fd fd;
    T *data;
    std::size_t n;
    bool readonly;

    public:
    size_t get_sizeof() const {
        return n * sizeof(T);
    }

    BlockStorage(const char *fname, bool readonly = true)
        : readonly(readonly) {
        size_t size;
        fd = open_data_file(fname, readonly, &size);

        // Calculate the number of elements
        if (size % sizeof(T) != 0) {
            throw std::runtime_error("File size is not a multiple of the element size for `" + std::string(fname) + "`.");
        }
        n = size / sizeof(T);

        data = static_cast<T*>(mmap_data(fd, fname, readonly, get_sizeof()));
    }

    BlockStorage(const char *fname, bool readonly, size_t n)
        : readonly(readonly), n(n) {
        fd = init_data_file(fname, readonly, get_sizeof());
        data = static_cast<T*>(mmap_data(fd, fname, readonly, get_sizeof()));
    }

    size_t read(size_t offset, T *out_data, size_t data_count) {
        return read_data(data, sizeof(T), n, out_data, offset, data_count);
    }

    size_t write(size_t offset, const T *in_data, size_t data_count) {
        return write_data(in_data, offset, data_count, data, sizeof(T), n, readonly);
    }

    void reset(){
        if (data && data != MAP_FAILED) {
            if (munmap(data, get_sizeof()) == -1) {
                show_sys_error("munmap");
                data = NULL;
                n = 0;
            }
        }
        fd.reset();
    }

    ~BlockStorage() {
        reset();
    }
};
