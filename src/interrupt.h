#pragma once

#include <stdbool.h>

void fnvcrack_clear_interrupt(void);
bool fnvcrack_interrupted(void);
int fnvcrack_install_interrupt_handler(void);
int fnvcrack_restore_interrupt_handler(void);
