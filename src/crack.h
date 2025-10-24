#pragma once
#include <stdbool.h>
#include <stdint.h>

#include <flint/fmpz.h>

#include "context.h"

enum CrackResult {
    // User provided a search length of 0
    BAD_SEARCH_LENGTH = -5,
    // User didn't initialize the context
    CONTEXT_UNINITIALIZED = -4,
    // When a user is trying to do a partial brute but didn't provide brute chars
    MISSING_BRUTE_CHARS = -3,
    // Failed on a memory allocation somewhere important
    MEMORY_ERROR = -2,
    // Failed to crack hash
    FAILED = -1,
    // Normal success
    SUCCESS = 0,
};
typedef enum CrackResult CrackResult;

#define ENUM_CASE(val) case val: return #val
inline const char* const result_as_str(CrackResult result) {
    switch (result) {
        ENUM_CASE(BAD_SEARCH_LENGTH);
        ENUM_CASE(CONTEXT_UNINITIALIZED);
        ENUM_CASE(MISSING_BRUTE_CHARS);
        ENUM_CASE(MEMORY_ERROR);
        ENUM_CASE(FAILED);
        ENUM_CASE(SUCCESS);
        default:
            return "UNKNOWN";
    }
}
#undef ENUM_CASE

CrackResult crack_u64_with_len(context_t ctx, uint64_t target, char_buffer* out_buffer, uint32_t expected_len, uint32_t brute_len);
CrackResult crack_fmpz_with_len(context_t ctx, fmpz_t target, char_buffer* out_buffer, uint32_t expected_len, uint32_t brute_len);
CrackResult crack_u64(context_t ctx, const uint64_t target, char_buffer* out_buffer, const uint32_t max_search_len, const uint64_t max_crack_len);
CrackResult crack_fmpz(context_t ctx, fmpz_t target, char_buffer* out_buffer, const uint32_t max_search_len, const uint64_t max_crack_len);