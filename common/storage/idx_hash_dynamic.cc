#include "idx_hash_dynamic.h"


hash_t hash_fast(hash_t mod, hash_compute_t value) {
    return value % mod;
}

hash_t hash_fast(hash_t mod, const byte *data, std::size_t len) {
    hash_compute_t result = 127;
    for (int it = 0; it < len; it++) {
        result = (result * 13 + data[it] + 7) % mod;
    }
    return result;
}

hash_t hash_fast(hash_t mod, const hash_t *data, std::size_t len) {
    hash_compute_t result = 191;
    for (int it = 0; it < len; it++) {
        result = (result * 127) % mod;
        result = (result + data[it] + it + 7) % mod;
    }
    return result;
}

hash_t hash_fast_set(hash_t mod, const hash_t *data, std::size_t len) {
    hash_compute_t result = 1;
    for (int it = 0; it < len; it++) {
        result = (result * data[it]) % mod;
    }
    return result;
}

void hash_table_append(hash_t hash, ) {
    //
}
