#include <stdlib.h>

#include "enumerate_native.h"
#include "interrupt.h"

typedef struct {
    int64_t* basis;
    int64_t* fit_min;
    int64_t* fit_max;
    int64_t* current;
    enumerate_solution_cb cb;
    void* cb_ctx;
    uint32_t dim;
    uint32_t enum_bound;
    uint64_t max_candidates;
    uint64_t candidates;
    enumerate_solver_result result;
} native_search_t;

bool enumerate_native_transition_fits_internal(int64_t value, uint32_t enum_bound) {
    if (enum_bound == 0) {
        return true;
    }

    const uint64_t magnitude = value < 0 ? UINT64_C(0) - (uint64_t)value : (uint64_t)value;
    const uint64_t scale = (uint64_t)enum_bound * 2;
    return magnitude <= (uint64_t)INT64_MAX / scale;
}

static bool _can_still_fit(native_search_t* ctx, uint32_t depth) {
    const size_t row = (size_t)depth * ctx->dim;
    for (uint32_t j = 0; j < ctx->dim; ++j) {
        if (ctx->current[j] < ctx->fit_min[row + j] || ctx->current[j] > ctx->fit_max[row + j]) {
            return false;
        }
    }

    return true;
}

static void _emit_candidate(native_search_t* ctx) {
    ++ctx->candidates;
    if (ctx->cb(ctx->current, ctx->dim, ctx->cb_ctx)) {
        ctx->result = ENUMERATE_SOLVER_FOUND;
    } else if (ctx->max_candidates != 0 && ctx->candidates >= ctx->max_candidates) {
        ctx->result = ENUMERATE_SOLVER_LIMIT;
    }
}

static void _add_basis_row(native_search_t* ctx, uint32_t row, int64_t scale) {
    if (scale == 0) {
        return;
    }

    const size_t start = (size_t)row * ctx->dim;
    for (uint32_t j = 0; j < ctx->dim; ++j) {
        ctx->current[j] += ctx->basis[start + j] * scale;
    }
}

static int64_t _floor_div(int64_t numerator, int64_t denominator) {
    int64_t quotient = numerator / denominator;
    const int64_t remainder = numerator % denominator;
    if (remainder != 0 && (remainder < 0) != (denominator < 0)) {
        --quotient;
    }
    return quotient;
}

static int64_t _ceil_div(int64_t numerator, int64_t denominator) {
    int64_t quotient = numerator / denominator;
    const int64_t remainder = numerator % denominator;
    if (remainder != 0 && (remainder < 0) == (denominator < 0)) {
        ++quotient;
    }
    return quotient;
}

static bool _coefficient_bounds(native_search_t* ctx, uint32_t depth, int64_t* lower, int64_t* upper) {
    int64_t lo = -(int64_t)ctx->enum_bound;
    int64_t hi = (int64_t)ctx->enum_bound;
    const size_t basis_row = (size_t)depth * ctx->dim;
    const size_t fit_row = (size_t)(depth + 1) * ctx->dim;

    for (uint32_t j = 0; j < ctx->dim && lo <= hi; ++j) {
        const int64_t row = ctx->basis[basis_row + j];
        const int64_t current = ctx->current[j];
        const int64_t min = ctx->fit_min[fit_row + j];
        const int64_t max = ctx->fit_max[fit_row + j];
        if (row == 0) {
            if (current < min || current > max) {
                return false;
            }
            continue;
        }

        const int64_t at_lo = current + row * lo;
        const int64_t at_hi = current + row * hi;
        int64_t candidate_lo = lo;
        int64_t candidate_hi = hi;
        if (row > 0) {
            if (min > at_hi || max < at_lo) {
                return false;
            }
            if (min > at_lo) {
                candidate_lo = _ceil_div(min - current, row);
            }
            if (max < at_hi) {
                candidate_hi = _floor_div(max - current, row);
            }
        } else {
            if (min > at_lo || max < at_hi) {
                return false;
            }
            if (max < at_lo) {
                candidate_lo = _ceil_div(max - current, row);
            }
            if (min > at_hi) {
                candidate_hi = _floor_div(min - current, row);
            }
        }

        if (candidate_lo > lo) {
            lo = candidate_lo;
        }
        if (candidate_hi < hi) {
            hi = candidate_hi;
        }
    }

    *lower = lo;
    *upper = hi;
    return lo <= hi;
}

static void _search(native_search_t* ctx, uint32_t depth) {
    if (ctx->result != ENUMERATE_SOLVER_DONE) {
        return;
    }
    if (fnvcrack_interrupted()) {
        ctx->result = ENUMERATE_SOLVER_INTERRUPTED;
        return;
    }

    int64_t lower, upper;
    if (!_coefficient_bounds(ctx, depth, &lower, &upper)) {
        return;
    }

    const uint32_t next_depth = depth + 1;
    bool zero_pending = lower <= 0 && upper >= 0;
    int64_t positive = lower > 1 ? lower : 1;
    int64_t negative = upper < -1 ? upper : -1;
    int64_t prev_off = 0;
    int64_t off;
    if (next_depth == ctx->dim) {
        while (enumerate_next_offset_internal(lower, upper, &zero_pending, &positive, &negative, &off)) {
            _add_basis_row(ctx, depth, off - prev_off);
            prev_off = off;

            if (fnvcrack_interrupted()) {
                ctx->result = ENUMERATE_SOLVER_INTERRUPTED;
            } else {
                _emit_candidate(ctx);
            }

            if (ctx->result != ENUMERATE_SOLVER_DONE) {
                _add_basis_row(ctx, depth, -prev_off);
                return;
            }
        }

        _add_basis_row(ctx, depth, -prev_off);
        return;
    }

    while (enumerate_next_offset_internal(lower, upper, &zero_pending, &positive, &negative, &off)) {
        _add_basis_row(ctx, depth, off - prev_off);
        prev_off = off;

        _search(ctx, next_depth);

        if (ctx->result != ENUMERATE_SOLVER_DONE) {
            _add_basis_row(ctx, depth, -prev_off);
            return;
        }
    }

    _add_basis_row(ctx, depth, -prev_off);
}

static bool _init_search(
    native_search_t* ctx,
    const fmpz_mat_t basis,
    const fmpz_mat_t base,
    const int64_t* lower_bounds,
    const int64_t* upper_bounds,
    uint32_t enum_bound
) {
    if ((uintmax_t)ctx->dim > (uintmax_t)SIZE_MAX) {
        return false;
    }
    const size_t n = (size_t)ctx->dim;
    const size_t max_items = SIZE_MAX / sizeof(*ctx->basis);
    if (n > max_items / n || n == SIZE_MAX || n + 1 > max_items / n) {
        return false;
    }
    const size_t matrix_size = n * n;
    const size_t bounds_size = (n + 1) * n;
    if (matrix_size > max_items - n || bounds_size > (max_items - matrix_size - n) / 2) {
        return false;
    }

    int64_t* native_basis = malloc((matrix_size + n + bounds_size * 2) * sizeof(*native_basis));
    if (!native_basis) {
        return false;
    }
    int64_t* native_current = native_basis + matrix_size;
    int64_t* native_fit_min = native_current + n;
    int64_t* native_fit_max = native_fit_min + bounds_size;

    bool fits = true;
    for (size_t i = 0; fits && i < n; ++i) {
        for (size_t j = 0; fits && j < n; ++j) {
            const fmpz* value = fmpz_mat_entry(basis, i, j);
            fits = fmpz_fits_si(value);
            if (fits) {
                const int64_t native_value = fmpz_get_si(value);
                fits = enumerate_native_transition_fits_internal(native_value, enum_bound);
                if (fits) {
                    native_basis[i * n + j] = native_value;
                }
            }
        }
    }

    for (size_t j = 0; fits && j < n; ++j) {
        const fmpz* value = fmpz_mat_entry(base, 0, j);
        fits = fmpz_fits_si(value);
        if (fits) {
            native_current[j] = fmpz_get_si(value);
            native_fit_min[n * n + j] = lower_bounds[j];
            native_fit_max[n * n + j] = upper_bounds[j];
        }
    }

    for (size_t j = 0; fits && j < n; ++j) {
        uint64_t tail = 0;
        for (size_t i = n; i-- > 0;) {
            const int64_t value = native_basis[i * n + j];
            const uint64_t magnitude = value < 0 ? (uint64_t)(-(value + 1)) + 1 : (uint64_t)value;
            const uint64_t scaled = magnitude * enum_bound;
            if (tail > (uint64_t)INT64_MAX - scaled) {
                fits = false;
                break;
            }
            tail += scaled;
            if (lower_bounds[j] < INT64_MIN + (int64_t)tail || upper_bounds[j] > INT64_MAX - (int64_t)tail) {
                fits = false;
                break;
            }
            native_fit_min[i * n + j] = lower_bounds[j] - (int64_t)tail;
            native_fit_max[i * n + j] = upper_bounds[j] + (int64_t)tail;
        }
        if (fits && (native_current[j] < INT64_MIN + (int64_t)tail || native_current[j] > INT64_MAX - (int64_t)tail)) {
            fits = false;
        }
    }

    if (fits) {
        ctx->basis = native_basis;
        ctx->current = native_current;
        ctx->fit_min = native_fit_min;
        ctx->fit_max = native_fit_max;
        ctx->enum_bound = enum_bound;
    } else {
        free(native_basis);
    }

    return fits;
}

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
) {
    native_search_t ctx = {
        .cb = cb,
        .cb_ctx = cb_ctx,
        .dim = dim,
        .max_candidates = max_candidates,
        .result = ENUMERATE_SOLVER_DONE,
    };
    if (!_init_search(&ctx, basis, base, lower_bounds, upper_bounds, enum_bound)) {
        return false;
    }

    if (_can_still_fit(&ctx, 0)) {
        _search(&ctx, 0);
    }

    *result = ctx.result;
    free(ctx.basis);
    return true;
}
