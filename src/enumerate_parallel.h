#pragma once

#include <stddef.h>

#include "enumerate.h"

#define ENUMERATE_PARALLEL_QUANTUM 256
#define ENUMERATE_PARALLEL_TASKS_PER_WORKER 32
#define ENUMERATE_PARALLEL_MAX_WORKERS 256

typedef struct enumerate_parallel_run_state enumerate_parallel_run_state;

typedef enum {
    ENUMERATE_TASK_MEMORY_ERROR = -1,
    ENUMERATE_TASK_COMPLETE = 0,
    ENUMERATE_TASK_PENDING = 1,
} enumerate_task_result;

typedef struct {
    void* cursor;
    enumerate_task_result (*advance)(
        void* cursor, uint32_t quantum, uint32_t worker_id, enumerate_parallel_run_state* run, uint32_t* visits_used
    );
    void (*destroy)(void* cursor);
} enumerate_parallel_task;

typedef struct {
    enumerate_parallel_task* tasks;
    size_t capacity;
    size_t head;
    size_t count;
} enumerate_parallel_frontier;

typedef enumerate_task_result (*enumerate_parallel_expand_cb)(
    void* cursor, uint32_t quantum, enumerate_parallel_frontier* frontier, uint32_t* visits_used
);

/* Expansion reserves one slot for the checked-out residual cursor. */
void enumerate_parallel_frontier_push(enumerate_parallel_frontier* frontier, enumerate_parallel_task task);
enumerate_solver_result enumerate_parallel_prepare(
    enumerate_parallel_task root,
    uint32_t threads,
    enumerate_parallel_expand_cb expand,
    enumerate_parallel_task** tasks,
    size_t* task_count
);
uint64_t enumerate_parallel_mix(uint64_t value);
bool enumerate_parallel_emit(
    enumerate_parallel_run_state* run, uint32_t worker_id, const int64_t* deltas, uint32_t dim
);
/* Takes ownership of tasks and their cursors, including on failure. */
enumerate_solver_result enumerate_parallel_run(
    enumerate_parallel_task* tasks,
    size_t task_count,
    uint32_t threads,
    uint64_t max_candidates,
    uint64_t shuffle_seed,
    enumerate_solution_cb cb,
    void* worker_contexts,
    size_t worker_context_size
);
/* Result metadata for the most recent run on the calling thread. */
uint32_t enumerate_parallel_winning_worker(void);

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
);

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
);

#ifdef FNVCRACK_ENUMERATE_PROBE
void enumerate_parallel_observe_turn(const void* cursor, enumerate_task_result result, uint32_t visits_used);
#endif
