#include <stdlib.h>
#include <string.h>

#include <flint/fmpz_lll.h>

#include "enumerate.h"
#include "interrupt.h"

typedef struct {
    const fmpz_mat_struct* basis;
    const fmpz_mat_struct* tail_abs;
    fmpz_mat_struct* current;
    const int64_t* lower_bounds;
    const int64_t* upper_bounds;
    enumerate_solution_cb cb;
    void* cb_ctx;
    int64_t* deltas;
    uint32_t dim;
    uint32_t enum_bound;
    uint64_t max_candidates;
    uint64_t candidates;
    bool found;
    bool hit_limit;
    bool interrupted;
} enum_search_t;

static void _mat_row_mul(fmpz_mat_t out, const fmpz_mat_t row, const fmpz_mat_t M) {
    const slong n = fmpz_mat_ncols(M);
    fmpz_t tmp;
    fmpz_init(tmp);

    for (slong j = 0; j < n; ++j) {
        fmpz_zero(fmpz_mat_entry(out, 0, j));
        for (slong i = 0; i < n; ++i) {
            fmpz_mul(tmp, fmpz_mat_entry(row, 0, i), fmpz_mat_entry(M, i, j));
            fmpz_add(fmpz_mat_entry(out, 0, j), fmpz_mat_entry(out, 0, j), tmp);
        }
    }

    fmpz_clear(tmp);
}

static void _row_norm(fmpz_t out, const fmpz_mat_t M, slong row) {
    fmpz_t tmp;
    fmpz_init(tmp);
    fmpz_zero(out);

    const slong n = fmpz_mat_ncols(M);
    for (slong j = 0; j < n; ++j) {
        fmpz_mul(tmp, fmpz_mat_entry(M, row, j), fmpz_mat_entry(M, row, j));
        fmpz_add(out, out, tmp);
    }

    fmpz_sqrt(out, out);
    fmpz_add_ui(out, out, 1);
    if (fmpz_is_zero(out)) {
        fmpz_one(out);
    }

    fmpz_clear(tmp);
}

static void _row_norm_sq(fmpz_t out, const fmpz_mat_t M, slong row) {
    fmpz_t tmp;
    fmpz_init(tmp);
    fmpz_zero(out);

    const slong n = fmpz_mat_ncols(M);
    for (slong j = 0; j < n; ++j) {
        fmpz_mul(tmp, fmpz_mat_entry(M, row, j), fmpz_mat_entry(M, row, j));
        fmpz_add(out, out, tmp);
    }

    fmpz_clear(tmp);
}

static void _swap_rows(fmpz_mat_t M, slong a, slong b) {
    if (a == b) {
        return;
    }

    const slong n = fmpz_mat_ncols(M);
    for (slong j = 0; j < n; ++j) {
        fmpz_swap(fmpz_mat_entry(M, a, j), fmpz_mat_entry(M, b, j));
    }
}

static void _sort_rows_desc(fmpz_mat_t M) {
    const slong n = fmpz_mat_nrows(M);
    fmpz_t ni, nj;
    fmpz_init(ni);
    fmpz_init(nj);

    for (slong i = 0; i < n; ++i) {
        for (slong j = i + 1; j < n; ++j) {
            _row_norm_sq(ni, M, i);
            _row_norm_sq(nj, M, j);
            if (fmpz_cmp(ni, nj) < 0) {
                _swap_rows(M, i, j);
            }
        }
    }

    fmpz_clear(nj);
    fmpz_clear(ni);
}

static bool _kannan_cvp_coords(
    fmpz_mat_t coords,
    const fmpz_mat_t basis,
    const fmpz_mat_t target
) {
    const slong n = fmpz_mat_ncols(basis);
    fmpz_mat_t L, U;
    fmpz_mat_init(L, n + 1, n + 1);
    fmpz_mat_init(U, n + 1, n + 1);

    for (slong i = 0; i < n; ++i) {
        for (slong j = 0; j < n; ++j) {
            fmpz_set(fmpz_mat_entry(L, i, j), fmpz_mat_entry(basis, i, j));
        }
    }
    for (slong j = 0; j < n; ++j) {
        fmpz_set(fmpz_mat_entry(L, n, j), fmpz_mat_entry(target, 0, j));
    }
    _row_norm(fmpz_mat_entry(L, n, n), basis, n - 1);

    fmpz_mat_one(U);
    fmpz_lll_t fl;
    fmpz_lll_context_init_default(fl);
    fmpz_lll_d(L, U, fl);

    bool found = false;
    for (slong i = 0; i <= n; ++i) {
        const fmpz* last = fmpz_mat_entry(U, i, n);
        if (!fmpz_equal_si(last, 1) && !fmpz_equal_si(last, -1)) {
            continue;
        }

        const slong sign = fmpz_sgn(last);
        for (slong j = 0; j < n; ++j) {
            fmpz_mul_si(fmpz_mat_entry(coords, 0, j), fmpz_mat_entry(U, i, j), -sign);
        }
        found = true;
        break;
    }

    fmpz_mat_clear(U);
    fmpz_mat_clear(L);
    return found;
}

static void _build_tail_abs(fmpz_mat_t tail_abs, const fmpz_mat_t basis, uint32_t enum_bound) {
    const slong n = fmpz_mat_ncols(basis);
    fmpz_t tmp;
    fmpz_init(tmp);

    for (slong j = 0; j < n; ++j) {
        fmpz_zero(fmpz_mat_entry(tail_abs, n, j));
    }

    for (slong i = n - 1; i >= 0; --i) {
        for (slong j = 0; j < n; ++j) {
            fmpz_abs(tmp, fmpz_mat_entry(basis, i, j));
            fmpz_mul_ui(tmp, tmp, enum_bound);
            fmpz_add(fmpz_mat_entry(tail_abs, i, j), fmpz_mat_entry(tail_abs, i + 1, j), tmp);
        }
    }

    fmpz_clear(tmp);
}

static bool _can_still_fit(const enum_search_t* ctx, uint32_t depth) {
    fmpz_t lo, hi;
    fmpz_init(lo);
    fmpz_init(hi);

    for (uint32_t j = 0; j < ctx->dim; ++j) {
        const fmpz* cur = fmpz_mat_entry(ctx->current, 0, j);
        const fmpz* rem = fmpz_mat_entry(ctx->tail_abs, depth, j);

        fmpz_sub(lo, cur, rem);
        fmpz_add(hi, cur, rem);
        if (fmpz_cmp_si(hi, ctx->lower_bounds[j]) < 0 ||
            fmpz_cmp_si(lo, ctx->upper_bounds[j]) > 0) {
            fmpz_clear(hi);
            fmpz_clear(lo);
            return false;
        }
    }

    fmpz_clear(hi);
    fmpz_clear(lo);
    return true;
}

static bool _emit_candidate(enum_search_t* ctx) {
    for (uint32_t j = 0; j < ctx->dim; ++j) {
        const fmpz* cur = fmpz_mat_entry(ctx->current, 0, j);
        if (fmpz_cmp_si(cur, ctx->lower_bounds[j]) < 0 ||
            fmpz_cmp_si(cur, ctx->upper_bounds[j]) > 0) {
            return false;
        }
        ctx->deltas[j] = fmpz_get_si(cur);
    }

    ++ctx->candidates;
    if (ctx->cb(ctx->deltas, ctx->dim, ctx->cb_ctx)) {
        ctx->found = true;
        return true;
    }

    if (ctx->max_candidates != 0 && ctx->candidates >= ctx->max_candidates) {
        ctx->hit_limit = true;
        return true;
    }

    return false;
}

static int64_t _offset_for_index(uint32_t idx) {
    if (idx == 0) {
        return 0;
    }

    const int64_t v = (idx + 1) / 2;
    return idx & 1 ? v : -v;
}

static void _add_basis_row(enum_search_t* ctx, uint32_t row, int64_t scale) {
    if (scale == 0) {
        return;
    }

    for (uint32_t j = 0; j < ctx->dim; ++j) {
        fmpz_addmul_si(
            fmpz_mat_entry(ctx->current, 0, j),
            fmpz_mat_entry(ctx->basis, row, j),
            scale
        );
    }
}

static void _search(enum_search_t* ctx, uint32_t depth) {
    if (ctx->found || ctx->hit_limit || ctx->interrupted) {
        return;
    }
    if (fnvcrack_interrupted()) {
        ctx->interrupted = true;
        return;
    }

    if (depth == ctx->dim) {
        _emit_candidate(ctx);
        return;
    }

    const uint32_t width = ctx->enum_bound * 2 + 1;
    for (uint32_t i = 0; i < width; ++i) {
        const int64_t off = _offset_for_index(i);
        _add_basis_row(ctx, depth, off);

        if (_can_still_fit(ctx, depth + 1)) {
            _search(ctx, depth + 1);
        }

        _add_basis_row(ctx, depth, -off);
        if (ctx->found || ctx->hit_limit || ctx->interrupted) {
            return;
        }
    }
}

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
) {
    const slong n = fmpz_mat_ncols(coeffs);
    if (n <= 0) {
        return ENUMERATE_SOLVER_DONE;
    }
    if (fnvcrack_interrupted()) {
        return ENUMERATE_SOLVER_INTERRUPTED;
    }

    int64_t* deltas = malloc((size_t)n * sizeof(*deltas));
    if (!deltas) {
        return ENUMERATE_SOLVER_MEMORY_ERROR;
    }

    fmpz_t inv, rhs_mod, tmp;
    fmpz_init(inv);
    fmpz_init(rhs_mod);
    fmpz_init(tmp);

    fmpz_mod(tmp, fmpz_mat_entry(coeffs, 0, 0), modulus);
    if (!fmpz_invmod(inv, tmp, modulus)) {
        fmpz_clear(tmp);
        fmpz_clear(rhs_mod);
        fmpz_clear(inv);
        free(deltas);
        return ENUMERATE_SOLVER_DONE;
    }

    fmpz_mat_t solution, basis, reduced, transform, target, coords, closest, base, tail_abs;
    fmpz_mat_init(solution, 1, n);
    fmpz_mat_init(basis, n, n);
    fmpz_mat_init(reduced, n, n);
    fmpz_mat_init(transform, n, n);
    fmpz_mat_init(target, 1, n);
    fmpz_mat_init(coords, 1, n);
    fmpz_mat_init(closest, 1, n);
    fmpz_mat_init(base, 1, n);
    fmpz_mat_init(tail_abs, n + 1, n);
    enumerate_solver_result result = ENUMERATE_SOLVER_DONE;

    fmpz_mod(rhs_mod, rhs, modulus);
    fmpz_mul(fmpz_mat_entry(solution, 0, 0), rhs_mod, inv);
    fmpz_mod(fmpz_mat_entry(solution, 0, 0), fmpz_mat_entry(solution, 0, 0), modulus);

    for (slong i = 1; i < n; ++i) {
        fmpz_mod(tmp, fmpz_mat_entry(coeffs, 0, i), modulus);
        fmpz_mul(tmp, tmp, inv);
        fmpz_mod(tmp, tmp, modulus);
        fmpz_neg(fmpz_mat_entry(basis, i - 1, 0), tmp);
        fmpz_one(fmpz_mat_entry(basis, i - 1, i));
    }
    fmpz_set(fmpz_mat_entry(basis, n - 1, 0), modulus);

    fmpz_mat_set(reduced, basis);
    fmpz_mat_one(transform);
    fmpz_lll_t fl;
    fmpz_lll_context_init_default(fl);
    fmpz_lll_d(reduced, transform, fl);
    if (fnvcrack_interrupted()) {
        result = ENUMERATE_SOLVER_INTERRUPTED;
        goto cleanup;
    }

    for (slong j = 0; j < n; ++j) {
        const int64_t mid = lower_bounds[j] + (upper_bounds[j] - lower_bounds[j]) / 2;
        fmpz_set_si(fmpz_mat_entry(target, 0, j), mid);
        fmpz_sub(fmpz_mat_entry(target, 0, j), fmpz_mat_entry(target, 0, j), fmpz_mat_entry(solution, 0, j));
    }

    if (!_kannan_cvp_coords(coords, reduced, target)) {
        fmpz_mat_zero(coords);
    }
    if (fnvcrack_interrupted()) {
        result = ENUMERATE_SOLVER_INTERRUPTED;
        goto cleanup;
    }

    _mat_row_mul(closest, coords, reduced);
    for (slong j = 0; j < n; ++j) {
        fmpz_add(fmpz_mat_entry(base, 0, j), fmpz_mat_entry(solution, 0, j), fmpz_mat_entry(closest, 0, j));
    }

    _sort_rows_desc(reduced);
    _build_tail_abs(tail_abs, reduced, enum_bound);

    enum_search_t search_ctx = {
        .basis = reduced,
        .tail_abs = tail_abs,
        .current = base,
        .lower_bounds = lower_bounds,
        .upper_bounds = upper_bounds,
        .cb = cb,
        .cb_ctx = cb_ctx,
        .deltas = deltas,
        .dim = (uint32_t)n,
        .enum_bound = enum_bound,
        .max_candidates = max_candidates,
        .candidates = 0,
        .found = false,
        .hit_limit = false,
        .interrupted = false,
    };
    if (_can_still_fit(&search_ctx, 0)) {
        _search(&search_ctx, 0);
    }

    result = ENUMERATE_SOLVER_DONE;
    if (search_ctx.interrupted) {
        result = ENUMERATE_SOLVER_INTERRUPTED;
    }
    else if (search_ctx.found) {
        result = ENUMERATE_SOLVER_FOUND;
    }
    else if (search_ctx.hit_limit) {
        result = ENUMERATE_SOLVER_LIMIT;
    }

cleanup:
    fmpz_mat_clear(tail_abs);
    fmpz_mat_clear(base);
    fmpz_mat_clear(closest);
    fmpz_mat_clear(coords);
    fmpz_mat_clear(target);
    fmpz_mat_clear(transform);
    fmpz_mat_clear(reduced);
    fmpz_mat_clear(basis);
    fmpz_mat_clear(solution);
    fmpz_clear(tmp);
    fmpz_clear(rhs_mod);
    fmpz_clear(inv);
    free(deltas);
    return result;
}
