#include <assert.h>
#include <stdlib.h>

#include <flint/thread_pool.h>

#include "enumerate_parallel.h"
#include "interrupt.h"

#ifdef _WIN32
#include <windows.h>
typedef struct {
    SRWLOCK mutex;
    CONDITION_VARIABLE condition;
} run_lock;
static bool _lock_init(run_lock* lock) {
    InitializeSRWLock(&lock->mutex);
    InitializeConditionVariable(&lock->condition);
    return true;
}
static void _lock_clear(run_lock* lock) {
    (void)lock;
}
static void _lock(run_lock* lock) {
    AcquireSRWLockExclusive(&lock->mutex);
}
static void _unlock(run_lock* lock) {
    ReleaseSRWLockExclusive(&lock->mutex);
}
static void _wait(run_lock* lock) {
    SleepConditionVariableSRW(&lock->condition, &lock->mutex, INFINITE, 0);
}
static void _signal(run_lock* lock) {
    WakeConditionVariable(&lock->condition);
}
static void _broadcast(run_lock* lock) {
    WakeAllConditionVariable(&lock->condition);
}
#else
#include <pthread.h>
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
} run_lock;
static bool _lock_init(run_lock* lock) {
    if (pthread_mutex_init(&lock->mutex, NULL) != 0) {
        return false;
    }
    if (pthread_cond_init(&lock->condition, NULL) != 0) {
        pthread_mutex_destroy(&lock->mutex);
        return false;
    }
    return true;
}
static void _lock_clear(run_lock* lock) {
    pthread_cond_destroy(&lock->condition);
    pthread_mutex_destroy(&lock->mutex);
}
static void _lock(run_lock* lock) {
    pthread_mutex_lock(&lock->mutex);
}
static void _unlock(run_lock* lock) {
    pthread_mutex_unlock(&lock->mutex);
}
static void _wait(run_lock* lock) {
    pthread_cond_wait(&lock->condition, &lock->mutex);
}
static void _signal(run_lock* lock) {
    pthread_cond_signal(&lock->condition);
}
static void _broadcast(run_lock* lock) {
    pthread_cond_broadcast(&lock->condition);
}
#endif

struct enumerate_parallel_run_state {
    enumerate_parallel_frontier queue;
    size_t live;
    run_lock lock;
    fnvcrack_cancel_token* token;
    enumerate_solver_result result;
    uint32_t winner;
    uint64_t max_candidates;
    uint64_t claimed;
    uint32_t in_flight;
    enumerate_solution_cb cb;
    void* contexts;
    size_t context_size;
};

typedef struct {
    enumerate_parallel_run_state* run;
    uint32_t id;
} parallel_worker;

#ifdef _MSC_VER
static __declspec(thread) uint32_t last_winner;
#else
static _Thread_local uint32_t last_winner;
#endif

uint32_t enumerate_parallel_winning_worker(void) {
    return last_winner;
}

uint64_t enumerate_parallel_mix(uint64_t value) {
    value += UINT64_C(0x9e3779b97f4a7c15);
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}

static void _shuffle(enumerate_parallel_task* tasks, size_t count, uint64_t seed) {
    for (size_t i = count; i > 1; --i) {
        const uint64_t threshold = (UINT64_C(0) - (uint64_t)i) % i;
        uint64_t draw;
        do {
            draw = enumerate_parallel_mix(seed);
            seed += UINT64_C(0x9e3779b97f4a7c15);
        } while (draw < threshold);
        const size_t j = draw % i;
        enumerate_parallel_task task = tasks[i - 1];
        tasks[i - 1] = tasks[j];
        tasks[j] = task;
    }
}

void enumerate_parallel_frontier_push(enumerate_parallel_frontier* frontier, enumerate_parallel_task task) {
    assert(frontier->count < frontier->capacity);
    frontier->tasks[(frontier->head + frontier->count++) % frontier->capacity] = task;
}

static enumerate_parallel_task _pop(enumerate_parallel_frontier* frontier) {
    enumerate_parallel_task task = frontier->tasks[frontier->head];
    frontier->head = (frontier->head + 1) % frontier->capacity;
    --frontier->count;
    return task;
}

static void _drain(enumerate_parallel_frontier* frontier) {
    while (frontier->count) {
        enumerate_parallel_task task = _pop(frontier);
        task.destroy(task.cursor);
    }
    free(frontier->tasks);
}

enumerate_solver_result enumerate_parallel_prepare(
    enumerate_parallel_task root,
    uint32_t threads,
    enumerate_parallel_expand_cb expand,
    enumerate_parallel_task** tasks,
    size_t* task_count
) {
    const size_t workers = threads < ENUMERATE_PARALLEL_MAX_WORKERS ? threads : ENUMERATE_PARALLEL_MAX_WORKERS;
    const size_t capacity = ENUMERATE_PARALLEL_TASKS_PER_WORKER * (workers ? workers : 1);
    enumerate_parallel_frontier frontier = {.capacity = capacity};
    frontier.tasks = malloc(capacity * sizeof(*frontier.tasks));
    *tasks = NULL;
    *task_count = 0;
    if (!frontier.tasks) {
        root.destroy(root.cursor);
        return ENUMERATE_SOLVER_MEMORY_ERROR;
    }
    enumerate_parallel_frontier_push(&frontier, root);
    uint64_t visits = 0;
    size_t idle = 0;
    enumerate_solver_result result = ENUMERATE_SOLVER_DONE;
    while (frontier.count && frontier.count < capacity && visits < capacity * ENUMERATE_PARALLEL_QUANTUM) {
        if (fnvcrack_interrupted()) {
            result = ENUMERATE_SOLVER_INTERRUPTED;
            break;
        }
        enumerate_parallel_task task = _pop(&frontier);
        uint32_t used = 0;
        const size_t before = frontier.count;
        enumerate_task_result state = expand(task.cursor, ENUMERATE_PARALLEL_QUANTUM, &frontier, &used);
        assert(used <= ENUMERATE_PARALLEL_QUANTUM);
        visits += used;
        if (state == ENUMERATE_TASK_PENDING) {
            enumerate_parallel_frontier_push(&frontier, task);
        } else {
            task.destroy(task.cursor);
        }
        if (state == ENUMERATE_TASK_MEMORY_ERROR) {
            result = ENUMERATE_SOLVER_MEMORY_ERROR;
            break;
        }
        idle = used || frontier.count != before + 1 ? 0 : idle + 1;
        if (idle >= frontier.count) {
            break;
        }
    }
    if (result == ENUMERATE_SOLVER_DONE && fnvcrack_interrupted()) {
        result = ENUMERATE_SOLVER_INTERRUPTED;
    }
    if (result != ENUMERATE_SOLVER_DONE) {
        _drain(&frontier);
        return result;
    }
    /* Compact the ring once; the runtime queue needs only its live capacity. */
    enumerate_parallel_task* ordered = malloc(frontier.count * sizeof(*ordered));
    if (frontier.count && !ordered) {
        _drain(&frontier);
        return ENUMERATE_SOLVER_MEMORY_ERROR;
    }
    *task_count = frontier.count;
    for (size_t i = 0; frontier.count; ++i) {
        ordered[i] = _pop(&frontier);
    }
    free(frontier.tasks);
    *tasks = ordered;
    return ENUMERATE_SOLVER_DONE;
}

static void _publish(enumerate_parallel_run_state* run, enumerate_solver_result result, uint32_t worker) {
    if (run->result == ENUMERATE_SOLVER_DONE ||
        (result == ENUMERATE_SOLVER_FOUND && run->result != ENUMERATE_SOLVER_FOUND)) {
        run->result = result;
        run->winner = worker;
        fnvcrack_cancel_token_request(run->token);
        _broadcast(&run->lock);
    }
}

bool enumerate_parallel_emit(
    enumerate_parallel_run_state* run, uint32_t worker_id, const int64_t* deltas, uint32_t dim
) {
    if (fnvcrack_interrupted()) {
        return true;
    }
    if (run->max_candidates) {
        _lock(&run->lock);
        while (run->result == ENUMERATE_SOLVER_DONE && run->claimed == run->max_candidates) {
            _wait(&run->lock);
        }
        if (run->result != ENUMERATE_SOLVER_DONE || fnvcrack_interrupted()) {
            _unlock(&run->lock);
            return true;
        }
        ++run->claimed;
        ++run->in_flight;
        _unlock(&run->lock);
    }
    void* context = run->contexts ? (char*)run->contexts + worker_id * run->context_size : NULL;
    const bool accepted = !fnvcrack_interrupted() && run->cb(deltas, dim, context);
    if (accepted || run->max_candidates) {
        _lock(&run->lock);
        if (run->max_candidates) {
            --run->in_flight;
        }
        if (accepted) {
            _publish(run, ENUMERATE_SOLVER_FOUND, worker_id);
        } else if (fnvcrack_interrupted()) {
            _publish(run, ENUMERATE_SOLVER_INTERRUPTED, UINT32_MAX);
        } else if (run->max_candidates && run->claimed == run->max_candidates && !run->in_flight) {
            _publish(run, ENUMERATE_SOLVER_LIMIT, UINT32_MAX);
        }
        _unlock(&run->lock);
    }
    return accepted || fnvcrack_interrupted();
}

static void _worker(void* userdata) {
    parallel_worker* worker = userdata;
    enumerate_parallel_run_state* run = worker->run;
    const fnvcrack_cancel_token* previous = fnvcrack_get_thread_cancel_token();
    fnvcrack_set_thread_cancel_token(run->token);
    _lock(&run->lock);
    for (;;) {
        while (!run->queue.count && run->live && run->result == ENUMERATE_SOLVER_DONE) {
            _wait(&run->lock);
        }
        if (fnvcrack_interrupted()) {
            _publish(run, ENUMERATE_SOLVER_INTERRUPTED, UINT32_MAX);
        }
        if (!run->live || run->result != ENUMERATE_SOLVER_DONE) {
            break;
        }
        enumerate_parallel_task task = _pop(&run->queue);
        _unlock(&run->lock);
        uint32_t visits = 0;
        enumerate_task_result state = task.advance(task.cursor, ENUMERATE_PARALLEL_QUANTUM, worker->id, run, &visits);
        assert(visits <= ENUMERATE_PARALLEL_QUANTUM);
#ifdef FNVCRACK_ENUMERATE_PROBE
        enumerate_parallel_observe_turn(task.cursor, state, visits);
#endif
        _lock(&run->lock);
        if (state == ENUMERATE_TASK_MEMORY_ERROR) {
            _publish(run, ENUMERATE_SOLVER_MEMORY_ERROR, UINT32_MAX);
        } else if (fnvcrack_interrupted()) {
            _publish(run, ENUMERATE_SOLVER_INTERRUPTED, UINT32_MAX);
        }
        if (state == ENUMERATE_TASK_PENDING && run->result == ENUMERATE_SOLVER_DONE) {
            enumerate_parallel_frontier_push(&run->queue, task);
            _signal(&run->lock);
        } else {
            --run->live;
            if (!run->live) {
                _broadcast(&run->lock);
            }
            _unlock(&run->lock);
            task.destroy(task.cursor);
            _lock(&run->lock);
        }
    }
    _unlock(&run->lock);
    fnvcrack_set_thread_cancel_token(previous);
    flint_cleanup();
}

enumerate_solver_result enumerate_parallel_run(
    enumerate_parallel_task* tasks,
    size_t task_count,
    uint32_t threads,
    uint64_t max_candidates,
    uint64_t shuffle_seed,
    enumerate_solution_cb cb,
    void* worker_contexts,
    size_t worker_context_size
) {
    last_winner = UINT32_MAX;
    enumerate_parallel_run_state run = {
        .queue = {.tasks = tasks, .capacity = task_count, .count = task_count},
        .live = task_count,
        .result = ENUMERATE_SOLVER_DONE,
        .winner = UINT32_MAX,
        .max_candidates = max_candidates,
        .cb = cb,
        .contexts = worker_contexts,
        .context_size = worker_context_size,
    };
    if (!task_count) {
        free(tasks);
        return fnvcrack_interrupted() ? ENUMERATE_SOLVER_INTERRUPTED : ENUMERATE_SOLVER_DONE;
    }
    if (fnvcrack_interrupted()) {
        _drain(&run.queue);
        return ENUMERATE_SOLVER_INTERRUPTED;
    }
    run.token = fnvcrack_cancel_token_new();
    if (!run.token || !_lock_init(&run.lock)) {
        fnvcrack_cancel_token_free(run.token);
        _drain(&run.queue);
        return ENUMERATE_SOLVER_MEMORY_ERROR;
    }
    _shuffle(tasks, task_count, shuffle_seed);
    uint32_t count = threads < ENUMERATE_PARALLEL_MAX_WORKERS ? threads : ENUMERATE_PARALLEL_MAX_WORKERS;
    if (count > task_count) {
        count = (uint32_t)task_count;
    }
    thread_pool_t pool;
    thread_pool_handle handles[ENUMERATE_PARALLEL_MAX_WORKERS];
    parallel_worker workers[ENUMERATE_PARALLEL_MAX_WORKERS];
    thread_pool_init(pool, count);
    const slong acquired = thread_pool_request(pool, handles, count);
    if (!acquired) {
        parallel_worker worker = {.run = &run};
        _worker(&worker);
    } else {
        for (slong i = 0; i < acquired; ++i) {
            workers[i] = (parallel_worker){.run = &run, .id = (uint32_t)i};
            thread_pool_wake(pool, handles[i], 0, _worker, &workers[i]);
        }
        for (slong i = 0; i < acquired; ++i) {
            thread_pool_wait(pool, handles[i]);
            thread_pool_give_back(pool, handles[i]);
        }
    }
    thread_pool_clear(pool);
    last_winner = run.winner;
    _drain(&run.queue);
    _lock_clear(&run.lock);
    fnvcrack_cancel_token_free(run.token);
    return run.result;
}
