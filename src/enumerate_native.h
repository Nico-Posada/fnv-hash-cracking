#pragma once

#include "enumerate.h"

static inline uint64_t enumerate_search_width(uint32_t enum_bound) {
    return (uint64_t)enum_bound * 2 + 1;
}

static inline int64_t enumerate_offset_for_index(uint64_t idx) {
    if (idx == 0) {
        return 0;
    }

    const int64_t value = (int64_t)((idx + 1) / 2);
    return idx & 1 ? value : -value;
}

bool enumerate_native_transition_fits_internal(int64_t value, uint32_t enum_bound);

bool enumerate_native_try(
    const fmpz_mat_t basis,
    const fmpz_mat_t base,
    const int64_t* lower_bounds,
    const int64_t* upper_bounds,
    uint32_t dim,
    uint32_t enum_bound,
    uint64_t max_candidates,
    enumerate_solution_cb cb,
    void* cb_ctx,
    enumerate_solver_result* result
);
