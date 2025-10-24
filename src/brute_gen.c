#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "brute_gen.h"

static const char* const _empty = "";

// private
typedef struct {
    const char* charset;
    char* buf_ptr;
    char* cur_product;
    const uint32_t repeat;
} _gen_ctx;

// private
void _generate(_gen_ctx* ctx, uint32_t depth) {
    if (depth == ctx->repeat) {
        memcpy(ctx->buf_ptr, ctx->cur_product, ctx->repeat);
        ctx->buf_ptr += ctx->repeat;
        return;
    }

    char c;
    for (int i = 0; (c = ctx->charset[i]) != 0; ++i) {
        ctx->cur_product[depth] = c;
        _generate(ctx, depth + 1);
    }
}

void product(brute_chars_t* out, const char* brute_charset, size_t brute_charset_len, uint32_t repeat) {
    if (!repeat || !brute_charset || brute_charset_len == 0) {
        out->buffer = (char*)_empty;
        out->entry_length = 0;
        out->total_entries = 1;
        return;
    }
    
    size_t arr_size = 1;
    for (uint32_t i = 0; i < repeat; ++i) {
        arr_size *= brute_charset_len;
    }

    char* ret_product = malloc(arr_size * repeat);
    if (!ret_product) {
        fprintf(stderr, "FATAL: Out of memory when generating product "
                        "with charset \"%s\" and repeat=%u\n", brute_charset, repeat);
        exit(2);
    }

    char cur_product[repeat+1];
    memset(cur_product, 0, repeat+1);

    _gen_ctx ctx = {
        .charset = brute_charset,
        .buf_ptr = ret_product,
        .cur_product = cur_product,
        .repeat = repeat,
    };

    _generate(&ctx, 0);

    out->buffer = ret_product;
    out->entry_length = repeat;
    out->total_entries = arr_size;
}

void destroy_product(brute_chars_t* out) {
    if (out->buffer && out->buffer != _empty) {
        free((void*)out->buffer);
        out->buffer = NULL;
    }
    
    out->entry_length = 0;
    out->total_entries = 0;
}