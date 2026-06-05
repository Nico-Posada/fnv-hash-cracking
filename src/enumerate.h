#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <flint/fmpz.h>
#include <flint/fmpz_mat.h>

typedef bool (*enumerate_solution_cb)(
    const int64_t* deltas,
    uint32_t delta_len,
    void* userdata
);

typedef enum {
    ENUMERATE_SOLVER_INTERRUPTED = -2,
    ENUMERATE_SOLVER_MEMORY_ERROR = -1,
    ENUMERATE_SOLVER_DONE = 0,
    ENUMERATE_SOLVER_FOUND = 1,
    ENUMERATE_SOLVER_LIMIT = 2,
} enumerate_solver_result;

enumerate_solver_result enumerate_bounded_mod(
    const fmpz_mat_t coeffs,
    const fmpz_t rhs,
    const fmpz_t modulus,
    const int64_t* lower_bounds,
    const int64_t* upper_bounds,
    uint32_t enum_bound,
    uint64_t max_candidates,
    enumerate_solution_cb cb,
    void* cb_ctx
);
