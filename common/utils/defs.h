#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)

typedef unsigned char byte_t;
