#include <stdlib.h>
#include <string.h>

#include "enumerate_native.h"
#include "interrupt.h"

typedef struct {
    int64_t* storage;
    int64_t* basis;
    int64_t* base;
    int64_t* fit_min;
    int64_t* fit_max;
    uint32_t dim;
    uint32_t enum_bound;
} native_prepared_t;

typedef struct {
    const native_prepared_t* prepared;
    int64_t* current;
    enumerate_solution_cb cb;
    void* cb_ctx;
    uint64_t max_candidates;
    uint64_t candidates;
    enumerate_solver_result result;
} native_search_t;

typedef struct {
    const native_prepared_t* prepared;
    int64_t* current;
    enumerate_cursor_frame* frames;
    uint32_t depth;
    uint32_t root_depth;
    bool done;
} native_cursor_t;

typedef enum {
    NATIVE_PREPARE_MEMORY_ERROR = -1,
    NATIVE_PREPARE_UNSUPPORTED = 0,
    NATIVE_PREPARE_READY = 1,
} native_prepare_result;

bool enumerate_native_transition_fits_internal(int64_t value, uint32_t enum_bound) {
    if (enum_bound == 0) {
        return true;
    }

    const uint64_t magnitude = value < 0 ? UINT64_C(0) - (uint64_t)value : (uint64_t)value;
    const uint64_t scale = (uint64_t)enum_bound * 2;
    return magnitude <= (uint64_t)INT64_MAX / scale;
}

static bool _can_still_fit(const native_prepared_t* prepared, const int64_t* current, uint32_t depth) {
    const size_t row = (size_t)depth * prepared->dim;
    for (uint32_t j = 0; j < prepared->dim; ++j) {
        if (current[j] < prepared->fit_min[row + j] || current[j] > prepared->fit_max[row + j]) {
            return false;
        }
    }

    return true;
}

static void _add_basis_row(const native_prepared_t* prepared, int64_t* current, uint32_t row, int64_t scale) {
    if (scale == 0) {
        return;
    }

    const size_t start = (size_t)row * prepared->dim;
    for (uint32_t j = 0; j < prepared->dim; ++j) {
        current[j] += prepared->basis[start + j] * scale;
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

static bool _coefficient_bounds(
    const native_prepared_t* prepared, const int64_t* current, uint32_t depth, int64_t* lower, int64_t* upper
) {
    int64_t lo = -(int64_t)prepared->enum_bound;
    int64_t hi = (int64_t)prepared->enum_bound;
    const size_t basis_row = (size_t)depth * prepared->dim;
    const size_t fit_row = (size_t)(depth + 1) * prepared->dim;

    for (uint32_t j = 0; j < prepared->dim && lo <= hi; ++j) {
        const int64_t row = prepared->basis[basis_row + j];
        const int64_t value = current[j];
        const int64_t min = prepared->fit_min[fit_row + j];
        const int64_t max = prepared->fit_max[fit_row + j];
        if (row == 0) {
            if (value < min || value > max) {
                return false;
            }
            continue;
        }

        const int64_t at_lo = value + row * lo;
        const int64_t at_hi = value + row * hi;
        int64_t candidate_lo = lo;
        int64_t candidate_hi = hi;
        if (row > 0) {
            if (min > at_hi || max < at_lo) {
                return false;
            }
            if (min > at_lo) {
                candidate_lo = _ceil_div(min - value, row);
            }
            if (max < at_hi) {
                candidate_hi = _floor_div(max - value, row);
            }
        } else {
            if (min > at_lo || max < at_hi) {
                return false;
            }
            if (max < at_lo) {
                candidate_lo = _ceil_div(max - value, row);
            }
            if (min > at_hi) {
                candidate_hi = _floor_div(min - value, row);
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

static void _emit_candidate(native_search_t* search) {
    ++search->candidates;
    if (search->cb(search->current, search->prepared->dim, search->cb_ctx)) {
        search->result = ENUMERATE_SOLVER_FOUND;
    } else if (search->max_candidates != 0 && search->candidates >= search->max_candidates) {
        search->result = ENUMERATE_SOLVER_LIMIT;
    }
}

static void _search(native_search_t* search, uint32_t depth) {
    if (search->result != ENUMERATE_SOLVER_DONE) {
        return;
    }
    if (fnvcrack_interrupted()) {
        search->result = ENUMERATE_SOLVER_INTERRUPTED;
        return;
    }

    int64_t lower, upper;
    if (!_coefficient_bounds(search->prepared, search->current, depth, &lower, &upper)) {
        return;
    }

    const uint32_t next_depth = depth + 1;
    enumerate_cursor_frame frame;
    enumerate_frame_init(&frame, lower, upper);
    int64_t offset;
    if (next_depth == search->prepared->dim) {
        while (enumerate_frame_next(&frame, &offset)) {
            _add_basis_row(search->prepared, search->current, depth, offset - frame.previous);
            frame.previous = offset;

            if (fnvcrack_interrupted()) {
                search->result = ENUMERATE_SOLVER_INTERRUPTED;
            } else {
                _emit_candidate(search);
            }

            if (search->result != ENUMERATE_SOLVER_DONE) {
                _add_basis_row(search->prepared, search->current, depth, -frame.previous);
                return;
            }
        }

        _add_basis_row(search->prepared, search->current, depth, -frame.previous);
        return;
    }

    while (enumerate_frame_next(&frame, &offset)) {
        _add_basis_row(search->prepared, search->current, depth, offset - frame.previous);
        frame.previous = offset;
        _search(search, next_depth);
        if (search->result != ENUMERATE_SOLVER_DONE) {
            _add_basis_row(search->prepared, search->current, depth, -frame.previous);
            return;
        }
    }

    _add_basis_row(search->prepared, search->current, depth, -frame.previous);
}

static native_prepare_result _prepare(
    native_prepared_t* prepared,
    const fmpz_mat_t basis,
    const fmpz_mat_t base,
    const int64_t* lower_bounds,
    const int64_t* upper_bounds,
    uint32_t dim,
    uint32_t enum_bound
) {
    const size_t n = dim;
    const size_t max_items = SIZE_MAX / sizeof(*prepared->storage);
    if (n == 0 || n > max_items / n || n == SIZE_MAX || n + 1 > max_items / n) {
        return NATIVE_PREPARE_UNSUPPORTED;
    }
    const size_t matrix_size = n * n;
    const size_t bounds_size = (n + 1) * n;
    if (matrix_size > max_items - n || bounds_size > (max_items - matrix_size - n) / 2) {
        return NATIVE_PREPARE_UNSUPPORTED;
    }

    int64_t* storage = malloc((matrix_size + n + bounds_size * 2) * sizeof(*storage));
    if (!storage) {
        return NATIVE_PREPARE_MEMORY_ERROR;
    }
    prepared->storage = storage;
    prepared->basis = storage;
    prepared->base = storage + matrix_size;
    prepared->fit_min = prepared->base + n;
    prepared->fit_max = prepared->fit_min + bounds_size;
    prepared->dim = dim;
    prepared->enum_bound = enum_bound;

    bool fits = true;
    for (size_t i = 0; fits && i < n; ++i) {
        for (size_t j = 0; fits && j < n; ++j) {
            const fmpz* value = fmpz_mat_entry(basis, i, j);
            fits = fmpz_fits_si(value);
            if (fits) {
                const int64_t native_value = fmpz_get_si(value);
                fits = enumerate_native_transition_fits_internal(native_value, enum_bound);
                if (fits) {
                    prepared->basis[i * n + j] = native_value;
                }
            }
        }
    }

    for (size_t j = 0; fits && j < n; ++j) {
        const fmpz* value = fmpz_mat_entry(base, 0, j);
        fits = fmpz_fits_si(value);
        if (fits) {
            prepared->base[j] = fmpz_get_si(value);
            prepared->fit_min[n * n + j] = lower_bounds[j];
            prepared->fit_max[n * n + j] = upper_bounds[j];
        }
    }

    for (size_t j = 0; fits && j < n; ++j) {
        uint64_t tail = 0;
        for (size_t i = n; i-- > 0;) {
            const int64_t value = prepared->basis[i * n + j];
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
            prepared->fit_min[i * n + j] = lower_bounds[j] - (int64_t)tail;
            prepared->fit_max[i * n + j] = upper_bounds[j] + (int64_t)tail;
        }
        if (fits && (prepared->base[j] < INT64_MIN + (int64_t)tail || prepared->base[j] > INT64_MAX - (int64_t)tail)) {
            fits = false;
        }
    }

    if (!fits) {
        free(prepared->storage);
        *prepared = (native_prepared_t){0};
        return NATIVE_PREPARE_UNSUPPORTED;
    }
    return NATIVE_PREPARE_READY;
}

static native_cursor_t* _cursor_new(const native_prepared_t* prepared, const int64_t* current, uint32_t root_depth) {
    const size_t n = prepared->dim;
    const size_t item_size = sizeof(int64_t) + sizeof(enumerate_cursor_frame);
    if (n > (SIZE_MAX - sizeof(native_cursor_t)) / item_size) {
        return NULL;
    }
    native_cursor_t* cursor = calloc(1, sizeof(*cursor) + n * item_size);
    if (!cursor) {
        return NULL;
    }
    cursor->prepared = prepared;
    cursor->current = (int64_t*)(cursor + 1);
    cursor->frames = (enumerate_cursor_frame*)(cursor->current + n);
    cursor->depth = root_depth;
    cursor->root_depth = root_depth;
    memcpy(cursor->current, current, n * sizeof(*current));
    return cursor;
}

static void _cursor_destroy(void* opaque) {
    free(opaque);
}

static enumerate_task_result _cursor_advance(
    void* opaque, uint32_t quantum, uint32_t worker_id, enumerate_parallel_run_state* run, uint32_t* visits_used
) {
    native_cursor_t* cursor = opaque;
    *visits_used = 0;
    if (cursor->done || fnvcrack_interrupted()) {
        cursor->done = true;
        return ENUMERATE_TASK_COMPLETE;
    }

    while (!cursor->done && *visits_used < quantum) {
        enumerate_cursor_frame* frame = &cursor->frames[cursor->depth];
        if (!frame->initialized) {
            int64_t lower, upper;
            if (_coefficient_bounds(cursor->prepared, cursor->current, cursor->depth, &lower, &upper)) {
                enumerate_frame_init(frame, lower, upper);
            } else {
                enumerate_frame_init(frame, 1, 0);
            }
        }

        int64_t offset;
        if (!enumerate_frame_next(frame, &offset)) {
            _add_basis_row(cursor->prepared, cursor->current, cursor->depth, -frame->previous);
            *frame = (enumerate_cursor_frame){0};
            if (cursor->depth == cursor->root_depth) {
                cursor->done = true;
            } else {
                --cursor->depth;
            }
            continue;
        }

        _add_basis_row(cursor->prepared, cursor->current, cursor->depth, offset - frame->previous);
        frame->previous = offset;
        ++*visits_used;
        if (fnvcrack_interrupted()) {
            cursor->done = true;
            break;
        }
        if (cursor->depth + 1 == cursor->prepared->dim) {
            if (enumerate_parallel_emit(run, worker_id, cursor->current, cursor->prepared->dim)) {
                cursor->done = true;
            }
        } else {
            ++cursor->depth;
            cursor->frames[cursor->depth] = (enumerate_cursor_frame){0};
        }
    }

    return cursor->done ? ENUMERATE_TASK_COMPLETE : ENUMERATE_TASK_PENDING;
}

static enumerate_parallel_task _cursor_task(native_cursor_t* cursor) {
    return (enumerate_parallel_task){
        .cursor = cursor,
        .advance = _cursor_advance,
        .destroy = _cursor_destroy,
    };
}

static enumerate_task_result
_cursor_expand(void* opaque, uint32_t quantum, enumerate_parallel_frontier* frontier, uint32_t* visits_used) {
    native_cursor_t* cursor = opaque;
    *visits_used = 0;
    if (cursor->done) {
        return ENUMERATE_TASK_COMPLETE;
    }
    if (fnvcrack_interrupted()) {
        return ENUMERATE_TASK_PENDING;
    }
    if (cursor->depth + 1 == cursor->prepared->dim) {
        return ENUMERATE_TASK_PENDING;
    }

    enumerate_cursor_frame* frame = &cursor->frames[cursor->depth];
    if (!frame->initialized) {
        int64_t lower, upper;
        if (_coefficient_bounds(cursor->prepared, cursor->current, cursor->depth, &lower, &upper)) {
            enumerate_frame_init(frame, lower, upper);
        } else {
            enumerate_frame_init(frame, 1, 0);
        }
    }

    while (*visits_used < quantum && frontier->count + 1 < frontier->capacity) {
        int64_t offset;
        if (!enumerate_frame_next(frame, &offset)) {
            return ENUMERATE_TASK_COMPLETE;
        }

        _add_basis_row(cursor->prepared, cursor->current, cursor->depth, offset - frame->previous);
        frame->previous = offset;
        ++*visits_used;
        if (fnvcrack_interrupted()) {
            return ENUMERATE_TASK_PENDING;
        }

        const uint32_t child_depth = cursor->depth + 1;
        if (_can_still_fit(cursor->prepared, cursor->current, child_depth)) {
            native_cursor_t* child = _cursor_new(cursor->prepared, cursor->current, child_depth);
            if (!child) {
                return ENUMERATE_TASK_MEMORY_ERROR;
            }
            enumerate_parallel_frontier_push(frontier, _cursor_task(child));
        }
    }

    return ENUMERATE_TASK_PENDING;
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
    native_prepared_t prepared = {0};
    if (_prepare(&prepared, basis, base, lower_bounds, upper_bounds, dim, enum_bound) != NATIVE_PREPARE_READY) {
        return false;
    }

    int64_t* current = prepared.base;
    native_search_t search = {
        .prepared = &prepared,
        .current = current,
        .cb = cb,
        .cb_ctx = cb_ctx,
        .max_candidates = max_candidates,
        .result = ENUMERATE_SOLVER_DONE,
    };
    if (_can_still_fit(&prepared, current, 0)) {
        _search(&search, 0);
    }

    *result = search.result;
    free(prepared.storage);
    return true;
}

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
) {
    native_prepared_t prepared = {0};
    const native_prepare_result prepared_result =
        _prepare(&prepared, basis, base, lower_bounds, upper_bounds, dim, enum_bound);
    if (prepared_result == NATIVE_PREPARE_UNSUPPORTED) {
        return false;
    }
    if (prepared_result == NATIVE_PREPARE_MEMORY_ERROR) {
        *result = ENUMERATE_SOLVER_MEMORY_ERROR;
        return true;
    }

    native_cursor_t* root = _cursor_new(&prepared, prepared.base, 0);
    if (!root) {
        free(prepared.storage);
        *result = ENUMERATE_SOLVER_MEMORY_ERROR;
        return true;
    }
    root->done = !_can_still_fit(&prepared, root->current, 0);

    enumerate_parallel_task* tasks;
    size_t task_count;
    *result = enumerate_parallel_prepare(_cursor_task(root), threads, _cursor_expand, &tasks, &task_count);
    if (*result == ENUMERATE_SOLVER_DONE) {
        *result = enumerate_parallel_run(
            tasks, task_count, threads, max_candidates, shuffle_seed, cb, worker_contexts, worker_context_size
        );
    }
    free(prepared.storage);
    return true;
}
