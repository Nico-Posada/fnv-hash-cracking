#pragma once

#include "enumerate.h"

static inline bool enumerate_next_offset_internal(
    int64_t lower,
    int64_t upper,
    bool* zero_pending,
    int64_t* positive,
    int64_t* negative,
    int64_t* offset
) {
    if (*zero_pending) {
        *zero_pending = false;
        *offset = 0;
        return true;
    }
    if (*positive <= upper && (*negative < lower || *positive <= -*negative)) {
        *offset = (*positive)++;
        return true;
    }
    if (*negative >= lower) {
        *offset = (*negative)--;
        return true;
    }
    return false;
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
