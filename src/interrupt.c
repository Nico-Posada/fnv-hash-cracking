#include "interrupt.h"

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#ifdef FNVCRACK_PYTHON_EXTENSION
#include <Python.h>
#endif

#if ATOMIC_INT_LOCK_FREE != 2
#error "fnvcrack requires always-lock-free atomic integers"
#endif

static atomic_int interrupt_requested = ATOMIC_VAR_INIT(0);
static atomic_flag lifecycle_lock = ATOMIC_FLAG_INIT;
static size_t lease_count = 0;
static struct sigaction saved_sigint_action;
static bool handler_installed = false;

static void lock_lifecycle(void) {
    while (atomic_flag_test_and_set_explicit(&lifecycle_lock, memory_order_acquire)) {
    }
}

static void unlock_lifecycle(void) {
    atomic_flag_clear_explicit(&lifecycle_lock, memory_order_release);
}

static void fnvcrack_sigint_handler(int sig) {
    (void)sig;

    int saved_errno = errno;
    atomic_store_explicit(&interrupt_requested, 1, memory_order_relaxed);
#ifdef FNVCRACK_PYTHON_EXTENSION
    (void)PyErr_SetInterruptEx(SIGINT);
#endif
    errno = saved_errno;
}

static bool owns_sigint_handler(const struct sigaction *action) {
    return (action->sa_flags & SA_SIGINFO) == 0
        && action->sa_handler == fnvcrack_sigint_handler;
}

static int fail_locked(int error) {
    unlock_lifecycle();
    errno = error;
    return -1;
}

static int finish_final_restore(void) {
    struct sigaction current_action;
    if (sigaction(SIGINT, NULL, &current_action) != 0) {
        return fail_locked(errno);
    }

    if (owns_sigint_handler(&current_action)
        && sigaction(SIGINT, &saved_sigint_action, NULL) != 0) {
        return fail_locked(errno);
    }

    handler_installed = false;
    memset(&saved_sigint_action, 0, sizeof(saved_sigint_action));
    unlock_lifecycle();
    return 0;
}

bool fnvcrack_interrupted(void) {
    return atomic_load_explicit(&interrupt_requested, memory_order_relaxed) != 0;
}

int fnvcrack_install_interrupt_handler(void) {
    lock_lifecycle();

    if (lease_count > 0) {
        struct sigaction current_action;
        if (sigaction(SIGINT, NULL, &current_action) != 0) {
            return fail_locked(errno);
        }
        if (!owns_sigint_handler(&current_action)) {
            return fail_locked(EBUSY);
        }
        if (lease_count == SIZE_MAX) {
            return fail_locked(EOVERFLOW);
        }

        ++lease_count;
        unlock_lifecycle();
        return 0;
    }

    if (handler_installed) {
        struct sigaction current_action;
        if (sigaction(SIGINT, NULL, &current_action) != 0) {
            return fail_locked(errno);
        }

        if (owns_sigint_handler(&current_action)) {
            atomic_store_explicit(&interrupt_requested, 0, memory_order_relaxed);
            lease_count = 1;
            unlock_lifecycle();
            return 0;
        }

        handler_installed = false;
        memset(&saved_sigint_action, 0, sizeof(saved_sigint_action));
    }

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = fnvcrack_sigint_handler;
    if (sigemptyset(&action.sa_mask) != 0) {
        return fail_locked(errno);
    }
    atomic_store_explicit(&interrupt_requested, 0, memory_order_relaxed);
    if (sigaction(SIGINT, &action, &saved_sigint_action) != 0) {
        return fail_locked(errno);
    }

    handler_installed = true;
    lease_count = 1;
    unlock_lifecycle();
    return 0;
}

int fnvcrack_restore_interrupt_handler(void) {
    lock_lifecycle();

    if (lease_count > 1) {
        --lease_count;
        unlock_lifecycle();
        return 0;
    }
    if (lease_count == 1) {
        lease_count = 0;
        return finish_final_restore();
    }
    if (handler_installed) {
        return finish_final_restore();
    }

    unlock_lifecycle();
    return 0;
}

#ifdef FNVCRACK_PYTHON_EXTENSION
int fnvcrack_sync_python_interrupt(void) {
    if (PyOS_InterruptOccurred() == 0) {
        return 0;
    }

    atomic_store_explicit(&interrupt_requested, 1, memory_order_relaxed);
    return PyErr_SetInterruptEx(SIGINT);
}
#endif
