#pragma once

#include "enumerate.h"
#include "enumerate_parallel.h"

typedef struct {
    int64_t lo;
    int64_t hi;
    int64_t previous;
    int64_t positive;
    int64_t negative;
    bool zero_pending;
    bool initialized;
} enumerate_cursor_frame;

static inline bool enumerate_next_offset_internal(
    int64_t lower, int64_t upper, bool* zero_pending, int64_t* positive, int64_t* negative, int64_t* offset
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

static inline void enumerate_frame_init(enumerate_cursor_frame* frame, int64_t lo, int64_t hi) {
    frame->lo = lo;
    frame->hi = hi;
    frame->previous = 0;
    frame->positive = lo > 1 ? lo : 1;
    frame->negative = hi < -1 ? hi : -1;
    frame->zero_pending = lo <= 0 && hi >= 0;
    frame->initialized = true;
}

static inline bool enumerate_frame_next(enumerate_cursor_frame* frame, int64_t* offset) {
    return enumerate_next_offset_internal(
        frame->lo, frame->hi, &frame->zero_pending, &frame->positive, &frame->negative, offset
    );
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

bool enumerate_native_parallel_try(
    const fmpz_mat_t basis,
    const fmpz_mat_t base,
    const int64_t* lower_bounds,
    const int64_t* upper_bounds,
    uint32_t dim,
    uint32_t enum_bound,
    uint64_t max_candidates,
    uint32_t threads,
    uint64_t shuffle_seed,
    enumerate_solution_cb cb,
    void* worker_contexts,
    size_t worker_context_size,
    enumerate_solver_result* result
);
