#pragma once

#include <stdbool.h>

typedef struct fnvcrack_cancel_token fnvcrack_cancel_token;

fnvcrack_cancel_token* fnvcrack_cancel_token_new(void);
void fnvcrack_cancel_token_free(fnvcrack_cancel_token* token);
void fnvcrack_cancel_token_request(fnvcrack_cancel_token* token);
void fnvcrack_set_thread_cancel_token(const fnvcrack_cancel_token* token);
const fnvcrack_cancel_token* fnvcrack_get_thread_cancel_token(void);

bool fnvcrack_interrupted(void);
int fnvcrack_install_interrupt_handler(void);
int fnvcrack_restore_interrupt_handler(void);

#ifdef FNVCRACK_PYTHON_EXTENSION
int fnvcrack_sync_python_interrupt(void);
#endif
