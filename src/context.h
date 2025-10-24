#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <flint/fmpz.h>

typedef struct _char_buffer {
    char* data;
    size_t length;
} char_buffer;

inline void clear_char_buffer(char_buffer* buf) {
    if (buf->data) {
        free(buf->data);
    }

    buf->data = NULL;
    buf->length = 0;
}

struct _context_s {
    union {
        // access with get_offset_basis
        uint64_t offset_basis;
        // access with get_offset_basis_fmpz
        fmpz_t offset_basis_fmpz;
    };
    union {
        // access with get_prime
        uint64_t prime;
        // access with get_prime_fmpz
        fmpz_t prime_fmpz;
    };
    // access with get_prefix
    char_buffer _prefix;
    // access with get_suffix
    char_buffer _suffix;
    // access with get_brute_chars
    char_buffer _brute_chars;
    uint8_t valid_chars[256];
    uint32_t bits;
    bool uses_fmpz;
    // access with is_initialized
    bool _initialized;
};

typedef struct _context_s context_t[1];
#define CREATE_CONTEXT(varname) context_t varname; memset(varname, 0, sizeof(context_t))

/* debug */

void print_context(context_t ctx);

/* setters */

bool set_suffix(context_t ctx, const char* suffix, size_t suffix_len);
bool set_prefix(context_t ctx, const char* prefix, size_t prefix_len);
bool set_brute_chars(context_t ctx, const char* brute_chars, size_t brute_chars_len);
void set_valid_chars(context_t ctx, const char* valid_chars, size_t valid_chars_len);
void set_offset_basis(context_t ctx, uint64_t offset_basis);
void set_offset_basis_fmpz(context_t ctx, fmpz_t offset_basis);
void set_prime(context_t ctx, uint64_t prime);
void set_prime_fmpz(context_t ctx, fmpz_t prime);

/* getters */

bool is_initialized(const context_t ctx);
char_buffer* get_prefix(const context_t ctx);
char_buffer* get_suffix(const context_t ctx);
char_buffer* get_brute_chars(const context_t ctx);
uint64_t get_offset_basis(const context_t ctx);
fmpz* get_offset_basis_fmpz(const context_t ctx);
uint64_t get_prime(const context_t ctx);
fmpz* get_prime_fmpz(const context_t ctx);

/* initializers */

bool init_crack_ctx(
    context_t ctx,
    uint64_t offset_basis,
    uint64_t prime,
    uint32_t bits,
    const char* brute_chars,
    const char* valid_chars,
    const char* prefix,
    const char* suffix
);

bool init_crack_ctx_with_len(
    context_t ctx,
    uint64_t offset_basis,
    uint64_t prime,
    uint32_t bits,
    const char* brute_chars,
    size_t brute_chars_len,
    const char* valid_chars,
    size_t valid_chars_len,
    const char* prefix,
    size_t prefix_len,
    const char* suffix,
    size_t suffix_len
);

bool init_crack_fmpz_ctx(
    context_t ctx,
    fmpz_t offset_basis,
    fmpz_t prime,
    uint32_t bits,
    const char* brute_chars,
    const char* valid_chars,
    const char* prefix,
    const char* suffix
);

bool init_crack_fmpz_ctx_with_len(
    context_t ctx,
    fmpz_t offset_basis,
    fmpz_t prime,
    uint32_t bits,
    const char* brute_chars,
    size_t brute_chars_len,
    const char* valid_chars,
    size_t valid_chars_len,
    const char* prefix,
    size_t prefix_len,
    const char* suffix,
    size_t suffix_len
);

/* destructors */

void destroy_crack_ctx(context_t ctx);