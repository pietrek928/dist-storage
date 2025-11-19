#include "idx_sort_static.h"


void compute_stage_ends(
    size_t n, size_t *stage_ends,
    const size_t *stage_size_steps, int nstages
) {
    for (int stage = nstages - 1; stage >= 1; stage--) {
        auto upper_items = n / (stage_size_steps[stage-1] + 1);
        stage_ends[stage] = n - upper_items;
        n = upper_items;
    }
    stage_ends[0] = n; // item counts in stage_ends now

    size_t pos = 0;
    for (int stage = 0; stage < nstages; stage++) {
        stage_ends[stage] += pos;
        pos = stage_ends[stage];
    }
}

size_t get_idx_position(
    size_t array_idx,
    const size_t *stage_ends, const size_t *stage_size_steps, int nstages
) {
    auto n = array_idx + 1;
    int stage = nstages - 1;
    while (stage >= 1 && n % (stage_size_steps[stage-1] + 1) == 0) {
        n /= (stage_size_steps[stage-1] + 1);
        stage--;
    }
    if (stage >= 1) {
        return stage_ends[stage-1] + (n - n / (stage_size_steps[stage-1] + 1)) - 1;
    } else {
        return n - 1;
    }
}

size_t get_stage_pos_items_count(
    size_t stage_pos, int stage, const size_t *stage_size_steps
) {
    size_t stage_nitems = stage_pos + 1;
    if (stage == 0) {
        return stage_nitems;
    } else {
        return stage_nitems + stage_nitems / (stage_size_steps[stage-1] + 1);
    }
}

size_t get_next_stage_pos(
    size_t stage_pos, int stage, const size_t *stage_size_steps
) {
    size_t nitems = get_stage_pos_items_count(stage_pos, stage, stage_size_steps);
    return (nitems-1) * stage_size_steps[stage];
}
