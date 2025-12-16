#include <cstdlib>
#include <cstdint>

typedef uint8_t byte;
typedef uint32_t hash_t;
typedef uint64_t hash_compute_t;

constexpr static std::size_t HASH_POS_NONE = -1;

hash_t hash_fast(hash_t mod, hash_compute_t value);
hash_t hash_fast(hash_t mod, const byte *data, std::size_t len);
hash_t hash_fast(hash_t mod, const hash_t *data, std::size_t len);
hash_t hash_fast_set(hash_t mod, const hash_t *data, std::size_t len);

typedef struct HashNode {
    std::size_t next;
    byte data[];
} HashNode;
