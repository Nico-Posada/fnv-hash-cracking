#pragma once

#include <stdint.h>

typedef struct {
    const char* buffer;
    uint32_t total_entries;
    uint32_t entry_length; 
} brute_chars_t;

void product(brute_chars_t* out, const char* brute_charset, size_t brute_charset_len, uint32_t repeat);
void destroy_product(brute_chars_t* out);