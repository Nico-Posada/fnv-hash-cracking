#pragma once

#include <stdbool.h>

bool fnvcrack_interrupted(void);
int fnvcrack_install_interrupt_handler(void);
int fnvcrack_restore_interrupt_handler(void);

#ifdef FNVCRACK_PYTHON_EXTENSION
int fnvcrack_sync_python_interrupt(void);
#endif
