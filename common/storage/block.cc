#include "block.h"
#include "unique_fd.h"
#include <cstddef>


typedef unsigned char byte;

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

size_t read_data(
    const void *data, size_t elem_size, size_t n,
    void *out_data, size_t offset, size_t data_count
) {
    if (offset > n) {
        return 0;
    }
    if (offset + data_count > n) {
        data_count = n - offset;
    }
    if (data_count == 0) {
        return 0;
    }
    std::copy(((byte*)data) + offset * elem_size, ((byte*)data) + (offset + data_count) * elem_size, (byte*)out_data);
    return data_count;
}

size_t write_data(
    const void *in_data, size_t offset, size_t data_count,
    void *data, size_t elem_size, size_t n, bool readonly
) {
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
    std::copy((byte*)in_data, ((byte*)in_data) + data_count * elem_size, data + offset * elem_size);
    return data_count;
}

unique_fd init_data_file(const char *fname, bool readonly, size_t size) {
    unique_fd fd = open(fname, get_open_flags(readonly, true), S_IRUSR | S_IWUSR);
    if (fd.valid()) {
        throw_sys_error("open or create file `" + std::string(fname) + "`");
    }

    // Truncate the file to the desired size. This also handles file creation.
    if (ftruncate(fd, size) == -1) {
        throw_sys_error("ftruncate file `" + std::string(fname) + "`");
    }
}

unique_fd open_data_file(const char *fname, bool readonly, size_t *size) {
    unique_fd fd = open(fname, get_open_flags(readonly, false));
    if (fd.valid()) {
        throw_sys_error("open file `" + std::string(fname) + "`");
    }

    // Get file size
    struct stat st;
    if (fstat(fd, &st) == -1) {
        throw_sys_error("fstat file `" + std::string(fname) + "`");
    }

    if (size) {
        *size = st.st_size;
    }
    return fd;
}

void *mmap_data(int fd, const char *fname, bool readonly, size_t size) {
    if (size == 0) {
        // Empty file, nothing to mmap
        return NULL;
    }

    // Memory map the file
    void *data = mmap(NULL, size, get_prot_flags(readonly), MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        throw_sys_error("mmap file `" + std::string(fname) + "`");
    }

    return data;
}
