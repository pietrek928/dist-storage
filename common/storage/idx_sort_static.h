#pragma once

#include <cstddef>
#include <tuple>


// tree_size = last_layer_items + all_items // (last_stage_step + 1)

void compute_stage_ends(
    size_t n, size_t *stage_ends,
    const size_t *stage_size_steps, int nstages
);
size_t get_idx_position(
    size_t array_idx,
    const size_t *stage_ends, const size_t *stage_size_steps, int nstages
);
size_t get_stage_pos_items_count(
    size_t stage_pos, int stage, const size_t *stage_size_steps
);
size_t get_next_stage_pos(
    size_t stage_pos, int stage, const size_t *stage_size_steps
);

// less_start(tree_idx) -> tree_idx<search_interval_start
// less_end(tree_idx) -> search_interval_end<tree_idx
template<typename Fless_start, typename Fless_end>
auto find_idx_range(
    const size_t *stage_ends, const size_t *stage_size_steps, int nstages,
    Fless_start &&less_start, Fless_end &&less_end
) {
    auto layer_idx_end = 0;
    while (layer_idx_end < stage_ends[0] && !less_end(layer_idx_end)) {
        layer_idx_end ++;
    }
    auto layer_idx_start = layer_idx_end;
    while (layer_idx_start > 0 && !less_start(layer_idx_start-1)) {
        layer_idx_start --;
    }

    auto result_start = layer_idx_start;
    auto result_end = layer_idx_end;

    for (int stage = 1; stage < nstages; stage++) {
        layer_idx_start = get_next_stage_pos(layer_idx_start, stage-1, stage_size_steps);
        layer_idx_end = get_next_stage_pos(layer_idx_end, stage-1, stage_size_steps);

        auto stage_start = stage_ends[stage-1];
        auto stage_end = stage_ends[stage];
        if (layer_idx_start > 0 && !less_start(stage_start+layer_idx_start-1)) {
            do {
                layer_idx_start --;
            } while (layer_idx_start > 0 && !less_start(stage_start+layer_idx_start-1));
            result_start = get_stage_pos_items_count(layer_idx_start, stage, stage_size_steps) - 1;
        } else {
            result_start = (result_start+1) * (stage_size_steps[stage-1] + 1);
        }
        if (layer_idx_end < stage_end && !less_end(stage_start+layer_idx_end)) {
            do {
                layer_idx_end ++;
            } while (layer_idx_end < stage_end && !less_end(stage_start+layer_idx_end));
            result_end = get_stage_pos_items_count(layer_idx_end, stage, stage_size_steps) - 1;
        } else {
            result_end = (result_end+1) * (stage_size_steps[stage-1] + 1);
        }
    }

    return std::make_tuple(result_start, result_end);
}
