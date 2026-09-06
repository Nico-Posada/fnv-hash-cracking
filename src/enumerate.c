#include <stdlib.h>

#include <flint/fmpz_lll.h>

#include "enumerate.h"
#include "enumerate_native.h"
#include "enumerate_parallel.h"
#include "interrupt.h"

typedef struct {
    const fmpz_mat_struct* basis;
    const fmpz_mat_struct* fit_min;
    const fmpz_mat_struct* fit_max;
    fmpz_mat_struct* current;
    fmpz* scratch_numerator;
    fmpz* scratch_quotient;
    enumerate_solution_cb cb;
    void* cb_ctx;
    int64_t* deltas;
    uint32_t dim;
    uint32_t enum_bound;
    uint64_t max_candidates;
    uint64_t candidates;
    enumerate_solver_result result;
} enum_search_t;

typedef struct {
    enum_search_t search;
    fmpz_mat_t current;
    fmpz_t scratch_numerator;
    fmpz_t scratch_quotient;
    enumerate_cursor_frame* frames;
    uint32_t depth;
    uint32_t root_depth;
    bool done;
} fmpz_cursor_t;

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

static void _sort_rows_desc(fmpz_mat_t M) {
    const slong n = fmpz_mat_nrows(M);
    fmpz_t ni, nj;
    fmpz_init(ni);
    fmpz_init(nj);

    for (slong i = 0; i < n; ++i) {
        _row_norm_sq(ni, M, i);
        for (slong j = i + 1; j < n; ++j) {
            _row_norm_sq(nj, M, j);
            if (fmpz_cmp(ni, nj) < 0) {
                fmpz_mat_swap_rows(M, NULL, i, j);
                fmpz_swap(ni, nj);
            }
        }
    }

    fmpz_clear(nj);
    fmpz_clear(ni);
}

static bool _kannan_cvp_coords(fmpz_mat_t coords, const fmpz_mat_t basis, const fmpz_mat_t target) {
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

static void _build_fit_bounds(
    fmpz_mat_t fit_min,
    fmpz_mat_t fit_max,
    const fmpz_mat_t tail_abs,
    const int64_t* lower_bounds,
    const int64_t* upper_bounds
) {
    const slong rows = fmpz_mat_nrows(tail_abs);
    const slong cols = fmpz_mat_ncols(tail_abs);

    for (slong i = 0; i < rows; ++i) {
        for (slong j = 0; j < cols; ++j) {
            fmpz_set_si(fmpz_mat_entry(fit_min, i, j), lower_bounds[j]);
            fmpz_sub(fmpz_mat_entry(fit_min, i, j), fmpz_mat_entry(fit_min, i, j), fmpz_mat_entry(tail_abs, i, j));

            fmpz_set_si(fmpz_mat_entry(fit_max, i, j), upper_bounds[j]);
            fmpz_add(fmpz_mat_entry(fit_max, i, j), fmpz_mat_entry(fit_max, i, j), fmpz_mat_entry(tail_abs, i, j));
        }
    }
}

static bool _can_still_fit(enum_search_t* ctx, uint32_t depth) {
    for (uint32_t j = 0; j < ctx->dim; ++j) {
        const fmpz* cur = fmpz_mat_entry(ctx->current, 0, j);
        if (fmpz_cmp(cur, fmpz_mat_entry(ctx->fit_min, depth, j)) < 0 ||
            fmpz_cmp(cur, fmpz_mat_entry(ctx->fit_max, depth, j)) > 0) {
            return false;
        }
    }

    return true;
}

static void _emit_candidate(enum_search_t* ctx) {
    for (uint32_t j = 0; j < ctx->dim; ++j) {
        ctx->deltas[j] = fmpz_get_si(fmpz_mat_entry(ctx->current, 0, j));
    }

    ++ctx->candidates;
    if (ctx->cb(ctx->deltas, ctx->dim, ctx->cb_ctx)) {
        ctx->result = ENUMERATE_SOLVER_FOUND;
    } else if (ctx->max_candidates != 0 && ctx->candidates >= ctx->max_candidates) {
        ctx->result = ENUMERATE_SOLVER_LIMIT;
    }
}

static void _add_basis_row(enum_search_t* ctx, uint32_t row, int64_t scale) {
    if (scale == 0) {
        return;
    }

    if (scale == 1) {
        for (uint32_t j = 0; j < ctx->dim; ++j) {
            fmpz_add(
                fmpz_mat_entry(ctx->current, 0, j),
                fmpz_mat_entry(ctx->current, 0, j),
                fmpz_mat_entry(ctx->basis, row, j)
            );
        }
    } else {
        for (uint32_t j = 0; j < ctx->dim; ++j) {
            fmpz_addmul_si(fmpz_mat_entry(ctx->current, 0, j), fmpz_mat_entry(ctx->basis, row, j), scale);
        }
    }
}

static bool _coefficient_bounds(enum_search_t* ctx, uint32_t depth, int64_t* lower, int64_t* upper) {
    int64_t lo = -(int64_t)ctx->enum_bound;
    int64_t hi = (int64_t)ctx->enum_bound;
    fmpz* numerator = ctx->scratch_numerator;
    fmpz* quotient = ctx->scratch_quotient;

    const uint32_t next_depth = depth + 1;
    for (uint32_t j = 0; j < ctx->dim && lo <= hi; ++j) {
        const fmpz* row = fmpz_mat_entry(ctx->basis, depth, j);
        const fmpz* current = fmpz_mat_entry(ctx->current, 0, j);
        if (fmpz_is_zero(row)) {
            if (fmpz_cmp(current, fmpz_mat_entry(ctx->fit_min, next_depth, j)) < 0 ||
                fmpz_cmp(current, fmpz_mat_entry(ctx->fit_max, next_depth, j)) > 0) {
                lo = 1;
                hi = 0;
            }
            continue;
        }

        const int sign = fmpz_sgn(row);
        const fmpz* min =
            sign > 0 ? fmpz_mat_entry(ctx->fit_min, next_depth, j) : fmpz_mat_entry(ctx->fit_max, next_depth, j);
        const fmpz* max =
            sign > 0 ? fmpz_mat_entry(ctx->fit_max, next_depth, j) : fmpz_mat_entry(ctx->fit_min, next_depth, j);

        fmpz_sub(numerator, min, current);
        fmpz_cdiv_q(quotient, numerator, row);
        if (fmpz_cmp_si(quotient, lo) > 0) {
            if (fmpz_cmp_si(quotient, hi) > 0) {
                lo = 1;
                hi = 0;
                break;
            }
            lo = fmpz_get_si(quotient);
        }

        fmpz_sub(numerator, max, current);
        fmpz_fdiv_q(quotient, numerator, row);
        if (fmpz_cmp_si(quotient, hi) < 0) {
            if (fmpz_cmp_si(quotient, lo) < 0) {
                lo = 1;
                hi = 0;
                break;
            }
            hi = fmpz_get_si(quotient);
        }
    }

    *lower = lo;
    *upper = hi;
    return lo <= hi;
}

static void _search(enum_search_t* ctx, uint32_t depth) {
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

static void _fmpz_cursor_destroy(void* opaque) {
    fmpz_cursor_t* cursor = opaque;
    if (!cursor) {
        return;
    }
    fmpz_clear(cursor->search.scratch_quotient);
    fmpz_clear(cursor->search.scratch_numerator);
    fmpz_mat_clear(cursor->search.current);
    free(cursor->search.deltas);
    free(cursor->frames);
    free(cursor);
}

static enumerate_task_result _fmpz_cursor_advance(
    void* opaque, uint32_t quantum, uint32_t worker_id, enumerate_parallel_run_state* run, uint32_t* visits_used
);

static enumerate_parallel_task _fmpz_cursor_task(fmpz_cursor_t* cursor) {
    return (enumerate_parallel_task){
        .cursor = cursor,
        .advance = _fmpz_cursor_advance,
        .destroy = _fmpz_cursor_destroy,
    };
}

static fmpz_cursor_t* _fmpz_cursor_new(const enum_search_t* shared, const fmpz_mat_t current, uint32_t depth) {
    fmpz_cursor_t* cursor = calloc(1, sizeof(*cursor));
    if (!cursor) {
        return NULL;
    }
    cursor->frames = calloc(shared->dim, sizeof(*cursor->frames));
    cursor->search.deltas = calloc(shared->dim, sizeof(*cursor->search.deltas));
    if (!cursor->frames || !cursor->search.deltas) {
        free(cursor->search.deltas);
        free(cursor->frames);
        free(cursor);
        return NULL;
    }

    cursor->search.current = cursor->current;
    cursor->search.scratch_numerator = cursor->scratch_numerator;
    cursor->search.scratch_quotient = cursor->scratch_quotient;
    fmpz_mat_init(cursor->search.current, 1, shared->dim);
    fmpz_mat_set(cursor->search.current, current);
    fmpz_init(cursor->search.scratch_numerator);
    fmpz_init(cursor->search.scratch_quotient);
    cursor->search.basis = shared->basis;
    cursor->search.fit_min = shared->fit_min;
    cursor->search.fit_max = shared->fit_max;
    cursor->search.dim = shared->dim;
    cursor->search.enum_bound = shared->enum_bound;
    cursor->depth = depth;
    cursor->root_depth = depth;
    return cursor;
}

static void _fmpz_frame_prepare(fmpz_cursor_t* cursor) {
    enumerate_cursor_frame* frame = &cursor->frames[cursor->depth];
    if (frame->initialized) {
        return;
    }
    int64_t lower;
    int64_t upper;
    if (!_coefficient_bounds(&cursor->search, cursor->depth, &lower, &upper)) {
        lower = 1;
        upper = 0;
    }
    enumerate_frame_init(frame, lower, upper);
}

static enumerate_task_result _fmpz_cursor_advance(
    void* opaque, uint32_t quantum, uint32_t worker_id, enumerate_parallel_run_state* run, uint32_t* visits_used
) {
    fmpz_cursor_t* cursor = opaque;
    *visits_used = 0;
    if (cursor->done || fnvcrack_interrupted()) {
        cursor->done = true;
        return ENUMERATE_TASK_COMPLETE;
    }

    while (!cursor->done && *visits_used < quantum) {
        _fmpz_frame_prepare(cursor);
        enumerate_cursor_frame* frame = &cursor->frames[cursor->depth];
        int64_t offset;
        if (!enumerate_frame_next(frame, &offset)) {
            _add_basis_row(&cursor->search, cursor->depth, -frame->previous);
            *frame = (enumerate_cursor_frame){0};
            if (cursor->depth == cursor->root_depth) {
                cursor->done = true;
            } else {
                --cursor->depth;
            }
            continue;
        }

        _add_basis_row(&cursor->search, cursor->depth, offset - frame->previous);
        frame->previous = offset;
        ++*visits_used;
        if (fnvcrack_interrupted()) {
            cursor->done = true;
            break;
        }
        if (cursor->depth + 1 == cursor->search.dim) {
            for (uint32_t j = 0; j < cursor->search.dim; ++j) {
                cursor->search.deltas[j] = fmpz_get_si(fmpz_mat_entry(cursor->search.current, 0, j));
            }
            if (fnvcrack_interrupted() ||
                enumerate_parallel_emit(run, worker_id, cursor->search.deltas, cursor->search.dim)) {
                cursor->done = true;
            }
        } else {
            ++cursor->depth;
            cursor->frames[cursor->depth] = (enumerate_cursor_frame){0};
        }
    }

    return cursor->done ? ENUMERATE_TASK_COMPLETE : ENUMERATE_TASK_PENDING;
}

static enumerate_task_result
_fmpz_cursor_expand(void* opaque, uint32_t quantum, enumerate_parallel_frontier* frontier, uint32_t* visits_used) {
    fmpz_cursor_t* cursor = opaque;
    *visits_used = 0;
    if (cursor->done) {
        return ENUMERATE_TASK_COMPLETE;
    }
    if (fnvcrack_interrupted()) {
        return ENUMERATE_TASK_PENDING;
    }
    if (cursor->depth + 1 == cursor->search.dim) {
        return ENUMERATE_TASK_PENDING;
    }

    _fmpz_frame_prepare(cursor);
    enumerate_cursor_frame* frame = &cursor->frames[cursor->depth];
    while (*visits_used < quantum && frontier->count + 1 < frontier->capacity) {
        int64_t offset;
        if (!enumerate_frame_next(frame, &offset)) {
            return ENUMERATE_TASK_COMPLETE;
        }

        _add_basis_row(&cursor->search, cursor->depth, offset - frame->previous);
        frame->previous = offset;
        ++*visits_used;
        if (fnvcrack_interrupted()) {
            return ENUMERATE_TASK_PENDING;
        }
        const uint32_t child_depth = cursor->depth + 1;
        if (_can_still_fit(&cursor->search, child_depth)) {
            fmpz_cursor_t* child = _fmpz_cursor_new(&cursor->search, cursor->search.current, child_depth);
            if (!child) {
                return ENUMERATE_TASK_MEMORY_ERROR;
            }
            enumerate_parallel_frontier_push(frontier, _fmpz_cursor_task(child));
        }
    }
    return ENUMERATE_TASK_PENDING;
}

enumerate_solver_result enumerate_fmpz_parallel_prepared(
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
    size_t worker_context_size
) {
    if (!dim) {
        return enumerate_parallel_run(
            NULL, 0, threads, max_candidates, shuffle_seed, cb, worker_contexts, worker_context_size
        );
    }
    if (fnvcrack_interrupted()) {
        return ENUMERATE_SOLVER_INTERRUPTED;
    }
    fmpz_mat_t tail_abs;
    fmpz_mat_t fit_min;
    fmpz_mat_t fit_max;
    fmpz_mat_init(tail_abs, (slong)dim + 1, dim);
    fmpz_mat_init(fit_min, (slong)dim + 1, dim);
    fmpz_mat_init(fit_max, (slong)dim + 1, dim);
    _build_tail_abs(tail_abs, basis, enum_bound);
    _build_fit_bounds(fit_min, fit_max, tail_abs, lower_bounds, upper_bounds);

    enum_search_t shared = {
        .basis = basis,
        .fit_min = fit_min,
        .fit_max = fit_max,
        .dim = dim,
        .enum_bound = enum_bound,
    };
    enumerate_parallel_task* tasks;
    size_t task_count;
    enumerate_solver_result result;
    fmpz_cursor_t* root = _fmpz_cursor_new(&shared, base, 0);
    if (!root) {
        result = ENUMERATE_SOLVER_MEMORY_ERROR;
    } else {
        root->done = !_can_still_fit(&root->search, 0);
        result = enumerate_parallel_prepare(_fmpz_cursor_task(root), threads, _fmpz_cursor_expand, &tasks, &task_count);
    }
    if (result == ENUMERATE_SOLVER_DONE) {
        result = enumerate_parallel_run(
            tasks, task_count, threads, max_candidates, shuffle_seed, cb, worker_contexts, worker_context_size
        );
    }

    fmpz_mat_clear(fit_max);
    fmpz_mat_clear(fit_min);
    fmpz_mat_clear(tail_abs);
    return result;
}

static enumerate_solver_result _enumerate_fmpz_serial_prepared(
    const fmpz_mat_t basis,
    const fmpz_mat_t base,
    const int64_t* lower_bounds,
    const int64_t* upper_bounds,
    uint32_t dim,
    uint32_t enum_bound,
    uint64_t max_candidates,
    enumerate_solution_cb cb,
    void* cb_ctx
) {
    int64_t* deltas = calloc(dim, sizeof(*deltas));
    if (!deltas) {
        return ENUMERATE_SOLVER_MEMORY_ERROR;
    }
    fmpz_mat_t tail_abs;
    fmpz_mat_t fit_min;
    fmpz_mat_t fit_max;
    fmpz_mat_init(tail_abs, (slong)dim + 1, dim);
    fmpz_mat_init(fit_min, (slong)dim + 1, dim);
    fmpz_mat_init(fit_max, (slong)dim + 1, dim);
    _build_tail_abs(tail_abs, basis, enum_bound);
    _build_fit_bounds(fit_min, fit_max, tail_abs, lower_bounds, upper_bounds);
    fmpz_t scratch_numerator;
    fmpz_t scratch_quotient;
    fmpz_init(scratch_numerator);
    fmpz_init(scratch_quotient);
    enum_search_t search_ctx = {
        .basis = basis,
        .fit_min = fit_min,
        .fit_max = fit_max,
        .current = (fmpz_mat_struct*)base,
        .scratch_numerator = scratch_numerator,
        .scratch_quotient = scratch_quotient,
        .cb = cb,
        .cb_ctx = cb_ctx,
        .deltas = deltas,
        .dim = dim,
        .enum_bound = enum_bound,
        .max_candidates = max_candidates,
        .result = ENUMERATE_SOLVER_DONE,
    };
    if (_can_still_fit(&search_ctx, 0)) {
        _search(&search_ctx, 0);
    }

    const enumerate_solver_result result = search_ctx.result;
    fmpz_clear(scratch_quotient);
    fmpz_clear(scratch_numerator);
    fmpz_mat_clear(fit_max);
    fmpz_mat_clear(fit_min);
    fmpz_mat_clear(tail_abs);
    free(deltas);
    return result;
}

static enumerate_solver_result _enumerate_bounded_mod_impl(
    const fmpz_mat_t coeffs,
    const fmpz_t rhs,
    const fmpz_t modulus,
    const int64_t* lower_bounds,
    const int64_t* upper_bounds,
    uint32_t enum_bound,
    uint64_t max_candidates,
    uint32_t threads,
    enumerate_solution_cb cb,
    void* worker_contexts,
    size_t worker_context_size,
    bool parallel
) {
    const slong n = fmpz_mat_ncols(coeffs);
    if (n <= 0) {
        return ENUMERATE_SOLVER_DONE;
    }
    if (fnvcrack_interrupted()) {
        return ENUMERATE_SOLVER_INTERRUPTED;
    }

    fmpz_t inv;
    fmpz_t rhs_mod;
    fmpz_t tmp;
    fmpz_init(inv);
    fmpz_init(rhs_mod);
    fmpz_init(tmp);

    fmpz_mod(tmp, fmpz_mat_entry(coeffs, 0, 0), modulus);
    if (!fmpz_invmod(inv, tmp, modulus)) {
        fmpz_clear(tmp);
        fmpz_clear(rhs_mod);
        fmpz_clear(inv);
        return ENUMERATE_SOLVER_DONE;
    }

    fmpz_mat_t solution;
    fmpz_mat_t basis;
    fmpz_mat_t reduced;
    fmpz_mat_t target;
    fmpz_mat_t coords;
    fmpz_mat_t closest;
    fmpz_mat_t base;
    fmpz_mat_init(solution, 1, n);
    fmpz_mat_init(basis, n, n);
    fmpz_mat_init(reduced, n, n);
    fmpz_mat_init(target, 1, n);
    fmpz_mat_init(coords, 1, n);
    fmpz_mat_init(closest, 1, n);
    fmpz_mat_init(base, 1, n);
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
    if (fnvcrack_interrupted()) {
        result = ENUMERATE_SOLVER_INTERRUPTED;
        goto cleanup;
    }
    fmpz_lll_t fl;
    fmpz_lll_context_init_default(fl);
    fmpz_lll_d(reduced, NULL, fl);
    if (fnvcrack_interrupted()) {
        result = ENUMERATE_SOLVER_INTERRUPTED;
        goto cleanup;
    }

    for (slong j = 0; j < n; ++j) {
        const int64_t mid = lower_bounds[j] + (upper_bounds[j] - lower_bounds[j]) / 2;
        fmpz_set_si(fmpz_mat_entry(target, 0, j), mid);
        fmpz_sub(fmpz_mat_entry(target, 0, j), fmpz_mat_entry(target, 0, j), fmpz_mat_entry(solution, 0, j));
    }
    if (fnvcrack_interrupted()) {
        result = ENUMERATE_SOLVER_INTERRUPTED;
        goto cleanup;
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
    if (fnvcrack_interrupted()) {
        result = ENUMERATE_SOLVER_INTERRUPTED;
        goto cleanup;
    }

    if (parallel) {
        const uint64_t seed = enumerate_parallel_mix((uint64_t)fmpz_get_ui(rhs_mod) ^ ((uint64_t)n << 32) ^ enum_bound);
        if (!enumerate_native_parallel_try(
                reduced,
                base,
                lower_bounds,
                upper_bounds,
                (uint32_t)n,
                enum_bound,
                max_candidates,
                threads,
                seed,
                cb,
                worker_contexts,
                worker_context_size,
                &result
            )) {
            result = enumerate_fmpz_parallel_prepared(
                reduced,
                base,
                lower_bounds,
                upper_bounds,
                (uint32_t)n,
                enum_bound,
                max_candidates,
                threads,
                seed,
                cb,
                worker_contexts,
                worker_context_size
            );
        }
    } else if (!enumerate_native_try(
                   reduced,
                   base,
                   lower_bounds,
                   upper_bounds,
                   (uint32_t)n,
                   enum_bound,
                   max_candidates,
                   cb,
                   worker_contexts,
                   &result
               )) {
        result = _enumerate_fmpz_serial_prepared(
            reduced, base, lower_bounds, upper_bounds, (uint32_t)n, enum_bound, max_candidates, cb, worker_contexts
        );
    }

cleanup:
    fmpz_mat_clear(base);
    fmpz_mat_clear(closest);
    fmpz_mat_clear(coords);
    fmpz_mat_clear(target);
    fmpz_mat_clear(reduced);
    fmpz_mat_clear(basis);
    fmpz_mat_clear(solution);
    fmpz_clear(tmp);
    fmpz_clear(rhs_mod);
    fmpz_clear(inv);
    return result;
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
    return _enumerate_bounded_mod_impl(
        coeffs, rhs, modulus, lower_bounds, upper_bounds, enum_bound, max_candidates, 1, cb, cb_ctx, 0, false
    );
}

enumerate_solver_result enumerate_bounded_mod_parallel(
    const fmpz_mat_t coeffs,
    const fmpz_t rhs,
    const fmpz_t modulus,
    const int64_t* lower_bounds,
    const int64_t* upper_bounds,
    uint32_t enum_bound,
    uint64_t max_candidates,
    uint32_t threads,
    enumerate_solution_cb cb,
    void* worker_contexts,
    size_t worker_context_size
) {
    return _enumerate_bounded_mod_impl(
        coeffs,
        rhs,
        modulus,
        lower_bounds,
        upper_bounds,
        enum_bound,
        max_candidates,
        threads,
        cb,
        worker_contexts,
        worker_context_size,
        true
    );
}
