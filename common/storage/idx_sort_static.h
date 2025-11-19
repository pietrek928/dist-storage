#pragma once

#include <cstddef>


// tree_size = last_layer_items + all_items // (last_stage_step + 1)

void compute_stage_ends(
    size_t n, size_t *stage_starts,
    const size_t *stage_size_steps, int nstages
);
size_t get_idx_position(
    size_t array_idx,
    const size_t *stage_starts, const size_t *stage_size_steps, int nstages
);
size_t get_next_stage_pos(
    size_t stage_pos, int stage, const size_t *stage_size_steps
);
