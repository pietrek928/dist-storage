#pragma once

#include <cstddef>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "unique_fd.h"


template<class T>
class BlockStorage {
    unique_fd fd;
    T *data;
    std::size_t n;
    bool readonly;

    int get_open_flags(bool readonly, bool create) {
        int flags = readonly ? O_RDONLY : O_RDWR;
        if (create) {
            flags |= O_CREAT;
        }
        return flags;
    }

    int get_prot_flags(bool readonly) {
        return readonly ? PROT_READ : PROT_READ | PROT_WRITE;
    }

    public:
    size_t get_sizeof() const {
        return n * sizeof(T);
    }

    BlockStorage(const char *fname, bool readonly = true)
        : readonly(readonly) {
        fd = open(fname, get_open_flags(readonly, false));
        if (fd.valid()) {
            throw_sys_error("open file `" + std::string(fname) + "`");
        }

        // Get file size
        struct stat st;
        if (fstat(fd, &st) == -1) {
            throw_sys_error("fstat file `" + std::string(fname) + "`");
        }

        // Calculate the number of elements
        if (st.st_size % sizeof(T) != 0) {
            throw std::runtime_error("File size is not a multiple of the element size for `" + std::string(fname) + "`.");
        }
        n = st.st_size / sizeof(T);
        if (n == 0) {
            // Empty file, nothing to map
            data = NULL;
            return;
        }

        // Memory map the file
        data = static_cast<T*>(mmap(NULL, get_sizeof(), get_prot_flags(readonly), MAP_SHARED, fd, 0));
        if (data == MAP_FAILED) {
            throw_sys_error("mmap file `" + std::string(fname) + "`");
        }
    }

    BlockStorage(const char *fname, bool readonly, size_t n)
        : readonly(readonly), n(n) {
        fd = open(fname, get_open_flags(readonly, true), S_IRUSR | S_IWUSR);
        if (fd.valid()) {
            throw_sys_error("open or create file `" + std::string(fname) + "`");
        }

        // Truncate the file to the desired size. This also handles file creation.
        if (ftruncate(fd, get_sizeof()) == -1) {
            throw_sys_error("ftruncate file `" + std::string(fname) + "`");
        }

        if (n == 0) {
            // Empty file, nothing to map
            data = NULL;
            return;
        }

        // Memory map the file
        data = static_cast<T*>(mmap(NULL, get_sizeof(), get_prot_flags(readonly), MAP_SHARED, fd, 0));
        if (data == MAP_FAILED) {
            throw_sys_error("mmap file `" + std::string(fname) + "`");
        }
    }

    size_t read(size_t offset, T *out_data, size_t data_count) {
        if (offset > n) {
            return 0;
        }
        if (offset + data_count > n) {
            data_count = n - offset;
        }
        if (data_count == 0) {
            return 0;
        }
        std::copy(data + offset, data + offset + data_count, out_data);
        return data_count;
    }

    size_t write(size_t offset, const T *in_data, size_t data_count) {
        if (offset > n) {
            return 0;
        }
        if (offset + data_count > n) {
            data_count = n - offset;
        }
        if (data_count == 0) {
            return 0;
        }
        if (readonly) {
            throw std::runtime_error("Cannot write to a readonly BlockStorage instance.");
        }
        std::copy(in_data, in_data + data_count, data + offset);
        return data_count;
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
