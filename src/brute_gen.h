#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "context.h"

typedef struct {
    const char* buffer;
    size_t total_entries;
    uint32_t entry_length;
} brute_chars_t;

bool product(brute_chars_t* out, char_buffer brute_charset, uint32_t repeat);
void destroy_product(brute_chars_t* out);
