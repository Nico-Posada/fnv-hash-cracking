#pragma once
#include <stdbool.h>
#include <stdint.h>

#include <flint/fmpz.h>

#include "context.h"

enum CrackResult {
    // User interrupted the crack.
    INTERRUPTED = -6,
    // User provided a search length of 0
    BAD_SEARCH_LENGTH = -5,
    // User didn't initialize the context
    CONTEXT_UNINITIALIZED = -4,
    // Failed on a memory allocation somewhere important
    MEMORY_ERROR = -2,
    // Failed to crack hash
    FAILED = -1,
    // Normal success
    SUCCESS = 0,
};
typedef enum CrackResult CrackResult;

#define CRACK_DEFAULT_ENUM_BOUND 4
#define CRACK_DEFAULT_MAX_ENUM_CANDIDATES 0

#define ENUM_CASE(val) case val: return #val
inline const char* const result_as_str(CrackResult result) {
    switch (result) {
        ENUM_CASE(INTERRUPTED);
        ENUM_CASE(BAD_SEARCH_LENGTH);
        ENUM_CASE(CONTEXT_UNINITIALIZED);
        ENUM_CASE(MEMORY_ERROR);
        ENUM_CASE(FAILED);
        ENUM_CASE(SUCCESS);
        default:
            return "UNKNOWN";
    }
}
#undef ENUM_CASE

CrackResult crack_u64_with_len_limits(context_t ctx, uint64_t target, char_buffer* out_buffer, uint32_t expected_len, uint32_t enum_bound, uint64_t max_enum_candidates);
CrackResult crack_fmpz_with_len_limits(context_t ctx, fmpz_t target, char_buffer* out_buffer, uint32_t expected_len, uint32_t enum_bound, uint64_t max_enum_candidates);
CrackResult crack_u64_limits(context_t ctx, const uint64_t target, char_buffer* out_buffer, const uint32_t max_search_len, uint32_t enum_bound, uint64_t max_enum_candidates);
CrackResult crack_fmpz_limits(context_t ctx, fmpz_t target, char_buffer* out_buffer, const uint32_t max_search_len, uint32_t enum_bound, uint64_t max_enum_candidates);

CrackResult crack_u64_with_len(context_t ctx, uint64_t target, char_buffer* out_buffer, uint32_t expected_len);
CrackResult crack_fmpz_with_len(context_t ctx, fmpz_t target, char_buffer* out_buffer, uint32_t expected_len);
CrackResult crack_u64(context_t ctx, const uint64_t target, char_buffer* out_buffer, const uint32_t max_search_len);
CrackResult crack_fmpz(context_t ctx, fmpz_t target, char_buffer* out_buffer, const uint32_t max_search_len);

void fnvcrack_clear_interrupt(void);
bool fnvcrack_interrupted(void);
int fnvcrack_install_interrupt_handler(void);
int fnvcrack_restore_interrupt_handler(void);
