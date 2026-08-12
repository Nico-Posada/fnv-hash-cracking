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
    uint64_t width;
    uint64_t max_candidates;
    uint64_t candidates;
    bool found;
    bool hit_limit;
    bool interrupted;
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
        ctx->found = true;
    } else if (ctx->max_candidates != 0 && ctx->candidates >= ctx->max_candidates) {
        ctx->hit_limit = true;
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

static void _search(native_search_t* ctx, uint32_t depth) {
    if (ctx->found || ctx->hit_limit || ctx->interrupted) {
        return;
    }
    if (fnvcrack_interrupted()) {
        ctx->interrupted = true;
        return;
    }

    const uint32_t next_depth = depth + 1;
    int64_t prev_off = 0;
    if (next_depth == ctx->dim) {
        for (uint64_t i = 0; i < ctx->width; ++i) {
            const int64_t off = enumerate_offset_for_index(i);
            _add_basis_row(ctx, depth, off - prev_off);
            prev_off = off;

            if (fnvcrack_interrupted()) {
                ctx->interrupted = true;
            } else if (_can_still_fit(ctx, next_depth)) {
                _emit_candidate(ctx);
            }

            if (ctx->found || ctx->hit_limit || ctx->interrupted) {
                _add_basis_row(ctx, depth, -prev_off);
                return;
            }
        }

        _add_basis_row(ctx, depth, -prev_off);
        return;
    }

    for (uint64_t i = 0; i < ctx->width; ++i) {
        const int64_t off = enumerate_offset_for_index(i);
        _add_basis_row(ctx, depth, off - prev_off);
        prev_off = off;

        if (_can_still_fit(ctx, next_depth)) {
            _search(ctx, next_depth);
        }

        if (ctx->found || ctx->hit_limit || ctx->interrupted) {
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
        ctx->width = enumerate_search_width(enum_bound);
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
    };
    if (!_init_search(&ctx, basis, base, lower_bounds, upper_bounds, enum_bound)) {
        return false;
    }

    if (_can_still_fit(&ctx, 0)) {
        _search(&ctx, 0);
    }

    *result = ENUMERATE_SOLVER_DONE;
    if (ctx.interrupted) {
        *result = ENUMERATE_SOLVER_INTERRUPTED;
    } else if (ctx.found) {
        *result = ENUMERATE_SOLVER_FOUND;
    } else if (ctx.hit_limit) {
        *result = ENUMERATE_SOLVER_LIMIT;
    }
    free(ctx.basis);
    return true;
}
