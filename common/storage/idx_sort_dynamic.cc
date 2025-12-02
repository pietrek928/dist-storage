#include "idx_sort_dynamic.h"

#include <algorithm>


Tpos rotate_left(
    IndexNodeHeader *up_node, Tpos up_node_pos,
    IndexNodeHeader *right_node, Tpos right_node_right_count
) {
    std::swap(up_node_pos, right_node->left);
    std::swap(up_node_pos, up_node->right);
    auto all_cnt = up_node->elem_count;
    up_node->elem_count = all_cnt - right_node_right_count - 1;
    right_node->elem_count = all_cnt;
    return up_node_pos;
}

Tpos rotate_right(
    IndexNodeHeader *up_node, Tpos up_node_pos,
    IndexNodeHeader *left_node, Tpos left_node_left_count
) {
    std::swap(up_node_pos, left_node->right);
    std::swap(up_node_pos, up_node->left);
    auto all_cnt = up_node->elem_count;
    up_node->elem_count = all_cnt - left_node_left_count - 1;
    left_node->elem_count = all_cnt;
    return up_node_pos;
}
