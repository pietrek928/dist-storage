#include <cstdint>

typedef int8_t byte;
typedef uint32_t Tpos;

constexpr static Tpos EMPTY = -1;

typedef struct IndexNodeHeader {
    Tpos left = EMPTY, right = EMPTY;
    Tpos elem_count = 1;

    byte data[];
} IndexNodeHeader;

Tpos rotate_left(
    IndexNodeHeader *up_node, Tpos up_node_pos,
    IndexNodeHeader *right_node, Tpos right_node_right_count
);
Tpos rotate_right(
    IndexNodeHeader *up_node, Tpos up_node_pos,
    IndexNodeHeader *left_node, Tpos left_node_left_count
);
