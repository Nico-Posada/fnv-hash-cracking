#include "interrupt.h"

#include <signal.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t interrupt_requested = 0;
static int handler_depth = 0;
static struct sigaction old_sigint_action;

static void fnvcrack_sigint_handler(int sig) {
    (void)sig;

    interrupt_requested = 1;

    static const char msg[] = "fnvcrack: interrupt requested\n";
    ssize_t written = write(STDERR_FILENO, msg, sizeof(msg) - 1);
    (void)written;
}

void fnvcrack_clear_interrupt(void) {
    interrupt_requested = 0;
}

bool fnvcrack_interrupted(void) {
    return interrupt_requested != 0;
}

int fnvcrack_install_interrupt_handler(void) {
    if (handler_depth++ > 0) {
        return 0;
    }

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = fnvcrack_sigint_handler;
    sigemptyset(&action.sa_mask);

    if (sigaction(SIGINT, &action, &old_sigint_action) != 0) {
        --handler_depth;
        return -1;
    }

    return 0;
}

int fnvcrack_restore_interrupt_handler(void) {
    if (handler_depth == 0) {
        return 0;
    }
    if (--handler_depth > 0) {
        return 0;
    }

    return sigaction(SIGINT, &old_sigint_action, NULL);
}
