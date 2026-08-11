#include "interrupt.h"

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef FNVCRACK_PYTHON_EXTENSION
#include <Python.h>
#endif

static void fnvcrack_sigint_handler(int sig);

#ifdef _WIN32
#include <windows.h>

typedef void (*signal_handler_t)(int);

static volatile LONG interrupt_requested = 0;
static volatile LONG lifecycle_lock = 0;
static signal_handler_t saved_sigint_handler;

static void lock_lifecycle(void) {
    while (InterlockedCompareExchange(&lifecycle_lock, 1, 0) != 0) {
    }
}

static void unlock_lifecycle(void) {
    (void)InterlockedExchange(&lifecycle_lock, 0);
}

static void set_interrupted(bool interrupted) {
    (void)InterlockedExchange(&interrupt_requested, interrupted ? 1 : 0);
}

static bool interrupted(void) {
    return InterlockedCompareExchange(&interrupt_requested, 0, 0) != 0;
}

static void rearm_interrupt_handler(void) {
    signal_handler_t displaced = signal(SIGINT, fnvcrack_sigint_handler);
    if (displaced != SIG_ERR
        && displaced != SIG_DFL
        && displaced != fnvcrack_sigint_handler) {
        (void)signal(SIGINT, displaced);
    }
}

static int owns_interrupt_handler(bool *owns_handler) {
    signal_handler_t current = signal(SIGINT, SIG_GET);
    if (current == SIG_ERR) {
        return -1;
    }

    *owns_handler = current == fnvcrack_sigint_handler;
    return 0;
}

static int install_interrupt_handler(void) {
    signal_handler_t previous = signal(SIGINT, fnvcrack_sigint_handler);
    if (previous == SIG_ERR) {
        return -1;
    }

    saved_sigint_handler = previous;
    return 0;
}

static int restore_interrupt_handler(void) {
    return signal(SIGINT, saved_sigint_handler) == SIG_ERR ? -1 : 0;
}

static void clear_saved_interrupt_handler(void) {
    saved_sigint_handler = NULL;
}

#else

#include <stdatomic.h>

#if ATOMIC_INT_LOCK_FREE != 2
#error "fnvcrack requires always-lock-free atomic integers"
#endif

static atomic_int interrupt_requested = ATOMIC_VAR_INIT(0);
static atomic_flag lifecycle_lock = ATOMIC_FLAG_INIT;
static struct sigaction saved_sigint_action;

static void lock_lifecycle(void) {
    while (atomic_flag_test_and_set_explicit(&lifecycle_lock, memory_order_acquire)) {
    }
}

static void unlock_lifecycle(void) {
    atomic_flag_clear_explicit(&lifecycle_lock, memory_order_release);
}

static void set_interrupted(bool interrupted) {
    atomic_store_explicit(
        &interrupt_requested,
        interrupted ? 1 : 0,
        memory_order_relaxed
    );
}

static bool interrupted(void) {
    return atomic_load_explicit(&interrupt_requested, memory_order_relaxed) != 0;
}

static void rearm_interrupt_handler(void) {
}

static bool action_owns_interrupt_handler(const struct sigaction *action) {
    return (action->sa_flags & SA_SIGINFO) == 0
        && action->sa_handler == fnvcrack_sigint_handler;
}

static int owns_interrupt_handler(bool *owns_handler) {
    struct sigaction current_action;
    if (sigaction(SIGINT, NULL, &current_action) != 0) {
        return -1;
    }

    *owns_handler = action_owns_interrupt_handler(&current_action);
    return 0;
}

static int install_interrupt_handler(void) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = fnvcrack_sigint_handler;
    if (sigemptyset(&action.sa_mask) != 0) {
        return -1;
    }

    return sigaction(SIGINT, &action, &saved_sigint_action);
}

static int restore_interrupt_handler(void) {
    return sigaction(SIGINT, &saved_sigint_action, NULL);
}

static void clear_saved_interrupt_handler(void) {
    memset(&saved_sigint_action, 0, sizeof(saved_sigint_action));
}

#endif

static size_t lease_count = 0;
static bool handler_installed = false;

static void fnvcrack_sigint_handler(int sig) {
    (void)sig;

    int saved_errno = errno;
    rearm_interrupt_handler();
    set_interrupted(true);
#ifdef FNVCRACK_PYTHON_EXTENSION
    (void)PyErr_SetInterruptEx(SIGINT);
#endif
    errno = saved_errno;
}

static int fail_locked(int error) {
    unlock_lifecycle();
    errno = error;
    return -1;
}

static int finish_final_restore(void) {
    bool owns_handler;
    if (owns_interrupt_handler(&owns_handler) != 0) {
        return fail_locked(errno);
    }

    if (owns_handler && restore_interrupt_handler() != 0) {
        return fail_locked(errno);
    }

    handler_installed = false;
    clear_saved_interrupt_handler();
    unlock_lifecycle();
    return 0;
}

bool fnvcrack_interrupted(void) {
    return interrupted();
}

int fnvcrack_install_interrupt_handler(void) {
    lock_lifecycle();

    if (lease_count > 0) {
        bool owns_handler;
        if (owns_interrupt_handler(&owns_handler) != 0) {
            return fail_locked(errno);
        }
        if (!owns_handler) {
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
        bool owns_handler;
        if (owns_interrupt_handler(&owns_handler) != 0) {
            return fail_locked(errno);
        }
        if (owns_handler) {
            set_interrupted(false);
            lease_count = 1;
            unlock_lifecycle();
            return 0;
        }

        handler_installed = false;
        clear_saved_interrupt_handler();
    }

    set_interrupted(false);
    if (install_interrupt_handler() != 0) {
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

    set_interrupted(true);
    return PyErr_SetInterruptEx(SIGINT);
}
#endif
