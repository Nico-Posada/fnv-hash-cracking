#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

#include "context.h"
#include "crack.h"
#include "enumerate.h"
#include "enumerate_native.h"
#include "enumerate_parallel.h"
#include "fnv.h"
#include "interrupt.h"
#include "inverse.h"
#include <flint/fmpz.h>

#define FNV64_OFFSET_BASIS 0xcbf29ce484222325ULL
#define FNV64_PRIME 0x100000001b3ULL

static volatile sig_atomic_t sentinel_called = 0;

static void sentinel_sigint_handler(int sig) {
    (void)sig;
    sentinel_called = 1;
}

static char_buffer buf(const char* data) {
    return (char_buffer){data, strlen(data)};
}

static void clear_result(char_buffer* out) {
    clear_char_buffer(out);
    out->data = NULL;
    out->length = 0;
}

static size_t current_address_space(void) {
    FILE* fp = fopen("/proc/self/statm", "r");
    if (!fp) {
        return 0;
    }

    size_t pages = 0;
    if (fscanf(fp, "%zu", &pages) != 1) {
        pages = 0;
    }
    fclose(fp);
    return pages * (size_t)sysconf(_SC_PAGESIZE);
}

static bool limit_address_space(size_t extra, struct rlimit* old_limit) {
    if (getrlimit(RLIMIT_AS, old_limit) != 0) {
        return false;
    }

    const size_t current = current_address_space();
    if (current == 0) {
        return false;
    }

    struct rlimit limit = *old_limit;
    limit.rlim_cur = current + extra;
    if (limit.rlim_max != RLIM_INFINITY && limit.rlim_cur > limit.rlim_max) {
        return false;
    }
    return setrlimit(RLIMIT_AS, &limit) == 0;
}

static void check_interrupt_api(void) {
    struct sigaction original_action;
    assert(sigaction(SIGINT, NULL, &original_action) == 0);

    struct sigaction sentinel_action;
    memset(&sentinel_action, 0, sizeof(sentinel_action));
    sentinel_action.sa_handler = sentinel_sigint_handler;
    assert(sigemptyset(&sentinel_action.sa_mask) == 0);
    assert(sigaddset(&sentinel_action.sa_mask, SIGTERM) == 0);
    sentinel_action.sa_flags = SA_RESTART;
    assert(sigaction(SIGINT, &sentinel_action, NULL) == 0);

    assert(fnvcrack_restore_interrupt_handler() == 0);
    assert(fnvcrack_install_interrupt_handler() == 0);
    assert(fnvcrack_install_interrupt_handler() == 0);
    assert(raise(SIGINT) == 0);
    assert(fnvcrack_interrupted());

    assert(fnvcrack_install_interrupt_handler() == 0);
    assert(fnvcrack_interrupted());
    assert(fnvcrack_restore_interrupt_handler() == 0);
    assert(fnvcrack_interrupted());
    assert(fnvcrack_restore_interrupt_handler() == 0);
    assert(fnvcrack_restore_interrupt_handler() == 0);

    struct sigaction restored_action;
    assert(sigaction(SIGINT, NULL, &restored_action) == 0);
    assert(restored_action.sa_handler == sentinel_sigint_handler);
    assert(sigismember(&restored_action.sa_mask, SIGTERM) == 1);
    assert((restored_action.sa_flags & SA_RESTART) != 0);

    sentinel_called = 0;
    assert(raise(SIGINT) == 0);
    assert(sentinel_called == 1);

    assert(fnvcrack_install_interrupt_handler() == 0);
    assert(!fnvcrack_interrupted());

    struct sigaction takeover_action;
    memset(&takeover_action, 0, sizeof(takeover_action));
    takeover_action.sa_handler = SIG_IGN;
    assert(sigemptyset(&takeover_action.sa_mask) == 0);
    assert(sigaction(SIGINT, &takeover_action, NULL) == 0);

    errno = 0;
    assert(fnvcrack_install_interrupt_handler() == -1);
    assert(errno == EBUSY);
    assert(fnvcrack_restore_interrupt_handler() == 0);
    assert(fnvcrack_restore_interrupt_handler() == 0);

    assert(sigaction(SIGINT, NULL, &restored_action) == 0);
    assert(restored_action.sa_handler == SIG_IGN);
    assert(sigaction(SIGINT, &sentinel_action, NULL) == 0);

    assert(sigaction(SIGINT, &original_action, NULL) == 0);
}

static void check_inverse_api(void) {
    assert(inverse(0, 8) == 0);
    assert(((uint8_t)(3 * inverse(3, 8))) == 1);
    assert(FNV64_PRIME * inverse(FNV64_PRIME, 64) == 1);

    fmpz_t value, result;
    fmpz_init(value);
    fmpz_init(result);

    fmpz_set_ui(value, 3);
    inverse_fmpz(result, value, 128);
    assert(!fmpz_is_zero(result));

    fmpz_set_ui(value, 2);
    inverse_fmpz(result, value, 128);
    assert(fmpz_is_zero(result));

    fmpz_clear(result);
    fmpz_clear(value);
}

static void check_fnv_api(void) {
    assert(
        fnv_u64("abc", FNV64_OFFSET_BASIS, FNV64_PRIME, 64) ==
        fnv_u64_with_len(buf("abc"), FNV64_OFFSET_BASIS, FNV64_PRIME, 64)
    );
    assert(fnv_u64("abc", FNV64_OFFSET_BASIS, FNV64_PRIME, 0) == 0);
    assert(fnv_u64("abc", FNV64_OFFSET_BASIS, FNV64_PRIME, 8) < 256);

    fmpz_t offset, prime, a, b;
    fmpz_init_set_ui(offset, FNV64_OFFSET_BASIS);
    fmpz_init_set_ui(prime, FNV64_PRIME);
    fmpz_init(a);
    fmpz_init(b);

    fnv_fmpz(a, "abc", offset, prime, 128);
    fnv_fmpz_with_len(b, buf("abc"), offset, prime, 128);
    assert(fmpz_equal(a, b));

    fmpz_clear(b);
    fmpz_clear(a);
    fmpz_clear(prime);
    fmpz_clear(offset);
}

static void check_context_api(void) {
    CREATE_CONTEXT(ctx);
    const char chars[] = "a\t\n\r\\\"\x01";
    const char prefix[] = "\t\n";
    const char suffix[] = "\r\\\"\x01";

    assert(!init_crack_ctx(ctx, FNV64_OFFSET_BASIS, FNV64_PRIME, 0, chars, prefix, suffix));
    assert(!init_crack_ctx(ctx, FNV64_OFFSET_BASIS, FNV64_PRIME, 65, chars, prefix, suffix));
    assert(init_crack_ctx(ctx, FNV64_OFFSET_BASIS, FNV64_PRIME, 64, chars, prefix, suffix));
    assert(set_prefix(ctx, *get_prefix(ctx)));
    assert(set_suffix(ctx, *get_suffix(ctx)));
    destroy_crack_ctx(ctx);

    fmpz_t offset, prime;
    fmpz_init_set_ui(offset, FNV64_OFFSET_BASIS);
    fmpz_init_set_ui(prime, FNV64_PRIME);

    CREATE_CONTEXT(fctx);
    assert(!init_crack_fmpz_ctx(fctx, offset, prime, 0, chars, prefix, suffix));
    assert(init_crack_fmpz_ctx(fctx, offset, prime, 128, chars, prefix, suffix));
    destroy_crack_ctx(fctx);

    fmpz_clear(prime);
    fmpz_clear(offset);
}

typedef struct {
    size_t calls;
    char first[2];
    char accepted[2];
} crack_callback_state;

static bool accept_second_crack_candidate(char_buffer candidate, void* userdata) {
    crack_callback_state* state = userdata;
    assert(candidate.length == 2);
    assert(fnv_u64_with_len(candidate, 0x25, 0xb3, 8) == 0xa5);

    state->calls++;
    if (state->calls == 1) {
        memcpy(state->first, candidate.data, candidate.length);
        return false;
    }

    assert(state->calls == 2);
    assert(memcmp(state->first, candidate.data, candidate.length) != 0);
    memcpy(state->accepted, candidate.data, candidate.length);
    return true;
}

static void check_crack_api(void) {
    char_buffer out = {NULL, 0};

    CREATE_CONTEXT(empty);
    assert(crack_u64(empty, 0, &out, 1) == CONTEXT_UNINITIALIZED);
    assert(crack_u64(empty, 0, &out, 0) == BAD_SEARCH_LENGTH);

    CREATE_CONTEXT(ctx);
    assert(init_crack_ctx(ctx, FNV64_OFFSET_BASIS, FNV64_PRIME, 64, "ab", "pre", "suf"));
    uint64_t target = fnv_u64("preabsuf", FNV64_OFFSET_BASIS, FNV64_PRIME, 64);

    assert(crack_u64_with_len(ctx, target, &out, 8) == SUCCESS);
    assert(out.length == 8);
    clear_result(&out);

    assert(crack_u64_with_len(ctx, target, &out, 5) == BAD_SEARCH_LENGTH);

    assert(crack_u64(ctx, target, &out, 2) == SUCCESS);
    assert(out.length == 8);
    clear_result(&out);

    assert(crack_u64_limits(ctx, 0, &out, 0, CRACK_DEFAULT_ENUM_BOUND, 1) == FAILED);
    destroy_crack_ctx(ctx);

    CREATE_CONTEXT(callback_ctx);
    assert(init_crack_ctx(callback_ctx, 0x25, 0xb3, 8, "abcdefghijklmnopqrstuvwxyz", NULL, NULL));
    crack_callback_state callback_state = {0};
    assert(
        crack_u64_with_len_callback_limits(
            callback_ctx, 0xa5, &out, 2, 255, 0, accept_second_crack_candidate, &callback_state
        ) == SUCCESS
    );
    assert(callback_state.calls == 2);
    assert(out.length == 2);
    assert(memcmp(out.data, callback_state.accepted, out.length) == 0);
    clear_result(&out);
    destroy_crack_ctx(callback_ctx);

    fmpz_t offset, prime, ftarget;
    fmpz_init_set_ui(offset, FNV64_OFFSET_BASIS);
    fmpz_init_set_ui(prime, FNV64_PRIME);
    fmpz_init(ftarget);

    CREATE_CONTEXT(fempty);
    fmpz_zero(ftarget);
    assert(crack_fmpz(fempty, ftarget, &out, 1) == CONTEXT_UNINITIALIZED);

    CREATE_CONTEXT(fctx);
    assert(init_crack_fmpz_ctx(fctx, offset, prime, 128, "ab", "pre", "suf"));
    fnv_fmpz(ftarget, "preabsuf", offset, prime, 128);

    assert(crack_fmpz_with_len(fctx, ftarget, &out, 8) == SUCCESS);
    assert(out.length == 8);
    clear_result(&out);

    assert(crack_fmpz_with_len(fctx, ftarget, &out, 5) == BAD_SEARCH_LENGTH);

    assert(crack_fmpz(fctx, ftarget, &out, 2) == SUCCESS);
    assert(out.length == 8);
    clear_result(&out);

    assert(crack_fmpz_limits(fctx, ftarget, &out, 0, CRACK_DEFAULT_ENUM_BOUND, 0) == FAILED);

    fmpz_zero(ftarget);
    assert(crack_fmpz(fctx, ftarget, &out, 1) == FAILED);

    destroy_crack_ctx(fctx);
    fmpz_clear(ftarget);
    fmpz_clear(prime);
    fmpz_clear(offset);
}

static void check_native_transition_bounds(void) {
    const int64_t edge = INT64_C(1) << 62;
    assert(!enumerate_native_transition_fits_internal(edge, 1));
    assert(!enumerate_native_transition_fits_internal(-edge, 1));
    assert(enumerate_native_transition_fits_internal(edge - 1, 1));
    assert(!enumerate_native_transition_fits_internal(INT64_MIN, 1));
    assert(enumerate_native_transition_fits_internal(INT64_MIN, 0));
}

static bool accept_second_candidate(const int64_t* deltas, uint32_t delta_len, void* userdata) {
    (void)deltas;
    (void)delta_len;
    return ++*(uint64_t*)userdata == 2;
}

static void check_enum_bound_width(void) {
    enum { DIM = 8 };
    fmpz_mat_t basis, base;
    fmpz_mat_init(basis, DIM, DIM);
    fmpz_mat_one(basis);
    fmpz_mat_init(base, 1, DIM);

    int64_t lower_bounds[DIM];
    int64_t upper_bounds[DIM];
    for (uint32_t i = 0; i < DIM; ++i) {
        lower_bounds[i] = -1;
        upper_bounds[i] = 1;
    }

    uint64_t candidates = 0;
    enumerate_solver_result result;
    assert(enumerate_native_try(
        basis,
        base,
        lower_bounds,
        upper_bounds,
        DIM,
        UINT32_C(1) << 31,
        1,
        accept_second_candidate,
        &candidates,
        &result
    ));
    assert(result == ENUMERATE_SOLVER_LIMIT);
    assert(candidates == 1);
    candidates = 0;
    assert(enumerate_native_try(
        basis,
        base,
        lower_bounds,
        upper_bounds,
        DIM,
        UINT32_C(1) << 31,
        2,
        accept_second_candidate,
        &candidates,
        &result
    ));
    assert(result == ENUMERATE_SOLVER_FOUND);
    assert(candidates == 2);

    fmpz_mat_clear(base);
    fmpz_mat_clear(basis);

    fmpz_mat_t coeffs;
    fmpz_mat_init(coeffs, 1, 1);
    fmpz_one(fmpz_mat_entry(coeffs, 0, 0));
    fmpz_t rhs, modulus;
    fmpz_init(rhs);
    fmpz_init_set_ui(modulus, 5);
    const int64_t lower_bound = -5;
    const int64_t upper_bound = 5;
    candidates = 0;
    assert(
        enumerate_bounded_mod(
            coeffs, rhs, modulus, &lower_bound, &upper_bound, UINT32_C(1) << 31, 1, accept_second_candidate, &candidates
        ) == ENUMERATE_SOLVER_LIMIT
    );
    assert(candidates == 1);
    candidates = 0;
    assert(
        enumerate_bounded_mod(
            coeffs, rhs, modulus, &lower_bound, &upper_bound, UINT32_C(1) << 31, 2, accept_second_candidate, &candidates
        ) == ENUMERATE_SOLVER_FOUND
    );
    assert(candidates == 2);
    fmpz_clear(modulus);
    fmpz_clear(rhs);
    fmpz_mat_clear(coeffs);
}

static void check_result_malloc_failure(void) {
    const size_t prefix_len = 8 * 1024 * 1024;
    char* prefix = malloc(prefix_len);
    assert(prefix);
    memset(prefix, 'a', prefix_len);

    CREATE_CONTEXT(ctx);
    assert(init_crack_ctx_with_len(
        ctx,
        FNV64_OFFSET_BASIS,
        FNV64_PRIME,
        64,
        (char_buffer){NULL, 0},
        (char_buffer){prefix, prefix_len},
        (char_buffer){NULL, 0}
    ));
    uint64_t target = fnv_u64_with_len((char_buffer){prefix, prefix_len}, FNV64_OFFSET_BASIS, FNV64_PRIME, 64);
    free(prefix);

    struct rlimit old_limit;
    if (limit_address_space(1024 * 1024, &old_limit)) {
        char_buffer out = {NULL, 0};
        assert(crack_u64_with_len(ctx, target, &out, (uint32_t)prefix_len) == MEMORY_ERROR);
        clear_result(&out);
        assert(setrlimit(RLIMIT_AS, &old_limit) == 0);
    }

    destroy_crack_ctx(ctx);
}

static bool observe_resumptions;
static const void* pending_cursors[32];
static unsigned pending_count, resumed_count;

void enumerate_parallel_observe_turn(const void* cursor, enumerate_task_result result, uint32_t visits_used) {
    assert(visits_used <= ENUMERATE_PARALLEL_QUANTUM);
    if (!observe_resumptions) {
        return;
    }
    unsigned i;
    for (i = 0; i < pending_count; ++i) {
        if (pending_cursors[i] == cursor) {
            ++resumed_count;
            break;
        }
    }
    if (result == ENUMERATE_TASK_PENDING && i == pending_count) {
        assert(pending_count < 32);
        pending_cursors[pending_count++] = cursor;
    }
}

typedef struct {
    unsigned id;
    unsigned turns;
} fifo_cursor;

static unsigned fifo_turns[8], fifo_count, fifo_destroyed;

static enumerate_task_result fifo_advance(
    void* opaque, uint32_t quantum, uint32_t worker, enumerate_parallel_run_state* run, uint32_t* visits_used
) {
    (void)worker;
    (void)run;
    fifo_cursor* cursor = opaque;
    assert(fifo_count < 8);
    fifo_turns[fifo_count++] = cursor->id;
    *visits_used = quantum;
    return ++cursor->turns == 2 ? ENUMERATE_TASK_COMPLETE : ENUMERATE_TASK_PENDING;
}

static void fifo_destroy(void* cursor) {
    ++fifo_destroyed;
    free(cursor);
}

static void check_scheduler_fifo(void) {
    fnvcrack_cancel_token* previous = fnvcrack_cancel_token_new();
    assert(previous);
    fnvcrack_set_thread_cancel_token(previous);
    for (uint32_t workers = 0; workers <= 1; ++workers) {
        enumerate_parallel_task* tasks = malloc(4 * sizeof(*tasks));
        assert(tasks);
        fifo_count = fifo_destroyed = 0;
        for (unsigned i = 0; i < 4; ++i) {
            fifo_cursor* cursor = calloc(1, sizeof(*cursor));
            assert(cursor);
            cursor->id = i;
            tasks[i] = (enumerate_parallel_task){cursor, fifo_advance, fifo_destroy};
        }
        assert(enumerate_parallel_run(tasks, 4, workers, 0, 123, NULL, NULL, 0) == ENUMERATE_SOLVER_DONE);
        assert(fifo_count == 8 && fifo_destroyed == 4);
        unsigned first_turns = 0;
        for (unsigned i = 0; i < 4; ++i) {
            assert(!(first_turns & (1u << fifo_turns[i])));
            first_turns |= 1u << fifo_turns[i];
            assert(fifo_turns[i] == fifo_turns[i + 4]);
        }
        assert(fnvcrack_get_thread_cancel_token() == previous);
        assert(!fnvcrack_interrupted());
    }
    fnvcrack_set_thread_cancel_token(NULL);
    fnvcrack_cancel_token_free(previous);
}

static bool record_delta(const int64_t* deltas, uint32_t dim, void* userdata) {
    unsigned* seen = userdata;
    assert(dim == 2 && deltas[1] == 0);
    assert(deltas[0] >= -5000 && deltas[0] <= 5000);
    ++seen[deltas[0] + 5000];
    return false;
}

static void check_parallel_cursor_multiplicity(void) {
    fmpz_mat_t basis, base;
    fmpz_mat_init(basis, 2, 2);
    fmpz_mat_one(basis);
    fmpz_mat_init(base, 1, 2);
    const int64_t lower[] = {-5000, 0}, upper[] = {5000, 0};
    unsigned serial[10001] = {0}, parallel[10001];
    enumerate_solver_result result;
    assert(enumerate_native_try(basis, base, lower, upper, 2, 5000, 0, record_delta, serial, &result));
    assert(result == ENUMERATE_SOLVER_DONE);
    for (unsigned i = 0; i < 10001; ++i) {
        assert(serial[i] == 1);
    }
    observe_resumptions = true;
    for (unsigned backend = 0; backend < 2; ++backend) {
        memset(parallel, 0, sizeof(parallel));
        pending_count = resumed_count = 0;
        if (backend == 0) {
            assert(enumerate_native_parallel_try(
                basis, base, lower, upper, 2, 5000, 0, 1, 123, record_delta, parallel, 0, &result
            ));
        } else {
            result = enumerate_fmpz_parallel_prepared(
                basis, base, lower, upper, 2, 5000, 0, 1, 123, record_delta, parallel, 0
            );
        }
        assert(result == ENUMERATE_SOLVER_DONE);
        assert(memcmp(serial, parallel, sizeof(serial)) == 0);
        assert(pending_count && resumed_count);
    }
    observe_resumptions = false;
    fmpz_mat_clear(base);
    fmpz_mat_clear(basis);
}

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    unsigned calls;
    bool first_returned;
    bool accept;
} limit_state;

typedef struct {
    limit_state* shared;
    unsigned order;
} limit_worker;

typedef struct {
    limit_worker* workers;
} limit_cursor;

static bool claimed_candidate(const int64_t* deltas, uint32_t dim, void* userdata) {
    (void)deltas;
    (void)dim;
    limit_worker* worker = userdata;
    limit_state* state = worker->shared;
    pthread_mutex_lock(&state->mutex);
    worker->order = ++state->calls;
    pthread_cond_broadcast(&state->condition);
    if (worker->order == 1) {
        while (state->calls < 2) {
            pthread_cond_wait(&state->condition, &state->mutex);
        }
    } else {
        assert(worker->order == 2);
        while (!state->first_returned) {
            pthread_cond_wait(&state->condition, &state->mutex);
        }
        assert(!fnvcrack_interrupted());
    }
    const bool accepted = worker->order == 2 && state->accept;
    pthread_mutex_unlock(&state->mutex);
    return accepted;
}

static enumerate_task_result
claimed_advance(void* opaque, uint32_t quantum, uint32_t id, enumerate_parallel_run_state* run, uint32_t* visits_used) {
    (void)quantum;
    limit_cursor* cursor = opaque;
    const int64_t delta = 0;
    *visits_used = 1;
    const bool stopped = enumerate_parallel_emit(run, id, &delta, 1);
    limit_worker* worker = &cursor->workers[id];
    if (worker->order == 1) {
        assert(!stopped);
        pthread_mutex_lock(&worker->shared->mutex);
        worker->shared->first_returned = true;
        pthread_cond_broadcast(&worker->shared->condition);
        pthread_mutex_unlock(&worker->shared->mutex);
    }
    return ENUMERATE_TASK_COMPLETE;
}

static void check_claimed_candidate_limit(void) {
    for (unsigned accept = 0; accept < 2; ++accept) {
        limit_state state = {
            .mutex = PTHREAD_MUTEX_INITIALIZER,
            .condition = PTHREAD_COND_INITIALIZER,
            .accept = accept,
        };
        limit_worker workers[2] = {{.shared = &state}, {.shared = &state}};
        enumerate_parallel_task* tasks = malloc(2 * sizeof(*tasks));
        assert(tasks);
        for (unsigned i = 0; i < 2; ++i) {
            limit_cursor* cursor = malloc(sizeof(*cursor));
            assert(cursor);
            cursor->workers = workers;
            tasks[i] = (enumerate_parallel_task){cursor, claimed_advance, free};
        }
        enumerate_solver_result result =
            enumerate_parallel_run(tasks, 2, 2, 2, 123, claimed_candidate, workers, sizeof(*workers));
        assert(result == (accept ? ENUMERATE_SOLVER_FOUND : ENUMERATE_SOLVER_LIMIT));
        assert(state.calls == 2 && state.first_returned);
        assert(!fnvcrack_interrupted());
        pthread_cond_destroy(&state.condition);
        pthread_mutex_destroy(&state.mutex);
    }
}

int main(void) {
    check_interrupt_api();
    check_inverse_api();
    check_fnv_api();
    check_context_api();
    check_crack_api();
    check_native_transition_bounds();
    check_result_malloc_failure();
    check_enum_bound_width();
    check_scheduler_fifo();
    check_parallel_cursor_multiplicity();
    check_claimed_candidate_limit();
    puts("Shared scheduler FIFO, resumption, multiplicity and candidate limits passed");
    return 0;
}