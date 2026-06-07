#include <stddef.h>
#include <stdlib.h>

#include "brute_gen.h"

static const char* const _empty = "";

bool product(brute_chars_t* out, const char* brute_charset, size_t brute_charset_len, uint32_t repeat) {
    out->buffer = NULL;
    out->entry_length = 0;
    out->total_entries = 0;

    if (!repeat || !brute_charset || brute_charset_len == 0) {
        out->buffer = (char*)_empty;
        out->entry_length = 0;
        out->total_entries = 1;
        return true;
    }
    
    size_t arr_size = 1;
    for (uint32_t i = 0; i < repeat; ++i) {
        if (arr_size > SIZE_MAX / brute_charset_len) {
            return false;
        }
        arr_size *= brute_charset_len;
    }

    if (arr_size > SIZE_MAX / repeat) {
        return false;
    }

    char* ret_product = malloc(arr_size * repeat);
    if (!ret_product) {
        return false;
    }

    for (size_t entry = 0; entry < arr_size; ++entry) {
        size_t idx = entry;
        char* dst = ret_product + entry * repeat;
        for (uint32_t pos = repeat; pos > 0; --pos) {
            dst[pos - 1] = brute_charset[idx % brute_charset_len];
            idx /= brute_charset_len;
        }
    }

    out->buffer = ret_product;
    out->entry_length = repeat;
    out->total_entries = arr_size;
    return true;
}

void destroy_product(brute_chars_t* out) {
    if (out->buffer && out->buffer != _empty) {
        free((void*)out->buffer);
        out->buffer = NULL;
    }
    
    out->entry_length = 0;
    out->total_entries = 0;
}
