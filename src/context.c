#include <stdlib.h>

#include "context.h"
#include "crack.h"
#include "fnv.h"

bool _init_common(
    context_t ctx,
    uint32_t bits,
    const char* brute_chars,
    size_t brute_chars_len,
    const char* valid_chars,
    size_t valid_chars_len,
    const char* prefix,
    size_t prefix_len,
    const char* suffix,
    size_t suffix_len
) {
    if (!set_prefix(ctx, prefix, prefix_len) ||
        !set_suffix(ctx, suffix, suffix_len) ||
        !set_brute_chars(ctx, brute_chars, brute_chars_len))
        return false;
    
    set_valid_chars(ctx, valid_chars, valid_chars_len);
    ctx->bits = bits;
    return true;
}

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
) {
    if (!_init_common(
        ctx, bits,
        brute_chars, brute_chars_len,
        valid_chars, valid_chars_len,
        prefix, prefix_len,
        suffix, suffix_len
    )) {
        return false;
    }

    ctx->uses_fmpz = false;
    set_offset_basis(ctx, offset_basis);
    set_prime(ctx, prime);
    ctx->_initialized = true;
    return true;
}

bool init_crack_ctx(
    context_t ctx,
    uint64_t offset_basis,
    uint64_t prime,
    uint32_t bits,
    const char* brute_chars,
    const char* valid_chars,
    const char* prefix,
    const char* suffix
) {
    return init_crack_ctx_with_len(
        ctx, offset_basis, prime, bits,
        brute_chars, brute_chars ? strlen(brute_chars) : 0,
        valid_chars, valid_chars ? strlen(valid_chars) : 0,
        prefix, prefix ? strlen(prefix) : 0,
        suffix, suffix ? strlen(suffix) : 0
    );
}

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
) {
    if (!_init_common(
        ctx, bits,
        brute_chars, brute_chars_len,
        valid_chars, valid_chars_len,
        prefix, prefix_len,
        suffix, suffix_len
    )) {
        return false;
    }

    ctx->uses_fmpz = true;
    fmpz_init(ctx->offset_basis_fmpz);
    fmpz_init(ctx->prime_fmpz);
    set_offset_basis_fmpz(ctx, offset_basis);
    set_prime_fmpz(ctx, prime);
    ctx->_initialized = true;
    return true;
}

bool init_crack_fmpz_ctx(
    context_t ctx,
    fmpz_t offset_basis,
    fmpz_t prime,
    uint32_t bits,
    const char* brute_chars,
    const char* valid_chars,
    const char* prefix,
    const char* suffix
) {
    return init_crack_fmpz_ctx_with_len(
        ctx, offset_basis, prime, bits,
        brute_chars, brute_chars ? strlen(brute_chars) : 0,
        valid_chars, valid_chars ? strlen(valid_chars) : 0,
        prefix, prefix ? strlen(prefix) : 0,
        suffix, suffix ? strlen(suffix) : 0
    );
}

void destroy_crack_ctx(context_t ctx) {
    if (!is_initialized(ctx)) {
        return;
    }

    if (ctx->uses_fmpz) {
        fmpz_clear(ctx->prime_fmpz);
        fmpz_clear(ctx->offset_basis_fmpz);
    }

    set_prefix(ctx, NULL, 0);
    set_suffix(ctx, NULL, 0);
    set_brute_chars(ctx, NULL, 0);
    set_offset_basis(ctx, 0);
    set_prime(ctx, 0);
    memset(ctx->valid_chars, 0, sizeof(ctx->valid_chars));
    ctx->bits = 0;
    ctx->_initialized = false;
}

inline bool _set_str_ref(char_buffer* ref, const char* str, size_t str_length) {
    if (ref->data == str || (ref->data && str && str_length == ref->length &&
        memcmp(ref->data, str, str_length) == 0)) {
        return true;
    }

    if (ref) {
        // assume it was last set by this func
        free(ref->data);
        ref->data = NULL;
        ref->length = 0;
    }
    
    if (str == NULL || str_length == 0) {
        ref->data = NULL;
        ref->length = 0;
        return true;
    }

    char* new_data = malloc(str_length + 1);
    if (!new_data) {
        ref->data = NULL;
        ref->length = 0;
        return false;
    }

    memcpy(new_data, str, str_length);
    new_data[str_length] = 0;
    ref->data = new_data;
    ref->length = str_length;
    return true;
}

inline void _set_table_data(uint8_t tbl[256], const char* valid_chars, size_t valid_chars_len) {
    for (size_t i = 0; i < valid_chars_len; ++i) {
        tbl[(uint8_t)valid_chars[i]] = 1;
    }
}

/* setters */

inline bool set_suffix(context_t ctx, const char* suffix, size_t suffix_len) {
    return _set_str_ref(get_suffix(ctx), suffix, suffix_len);
}

inline bool set_prefix(context_t ctx, const char* prefix, size_t prefix_len) {
    return _set_str_ref(get_prefix(ctx), prefix, prefix_len);
}

inline bool set_brute_chars(context_t ctx, const char* brute_chars, size_t brute_chars_len) {
    return _set_str_ref(get_brute_chars(ctx), brute_chars, brute_chars_len);
}

inline void set_valid_chars(context_t ctx, const char* valid_chars, size_t valid_chars_len) {
    if (valid_chars && valid_chars_len) {
        _set_table_data(ctx->valid_chars, valid_chars, valid_chars_len);
    }
    else {
        // special case, if valid chars is not provided, we assume all chars are valid
        memset(ctx->valid_chars, 1, sizeof(ctx->valid_chars));
    }
}

inline void set_offset_basis(context_t ctx, uint64_t offset_basis) {
    ctx->offset_basis = offset_basis;
}

inline void set_offset_basis_fmpz(context_t ctx, fmpz_t offset_basis) {
    fmpz_set(ctx->offset_basis_fmpz, offset_basis);
}

inline void set_prime(context_t ctx, uint64_t prime) {
    ctx->prime = prime;
}

inline void set_prime_fmpz(context_t ctx, fmpz_t prime) {
    fmpz_set(ctx->prime_fmpz, prime);
}

/* getters */

inline bool is_initialized(const context_t ctx) {
    return ctx->_initialized;
}

inline char_buffer* get_prefix(const context_t ctx) {
    return (char_buffer*)&ctx->_prefix;
}

inline char_buffer* get_suffix(const context_t ctx) {
    return (char_buffer*)&ctx->_suffix;
}

inline char_buffer* get_brute_chars(const context_t ctx) {
    return (char_buffer*)&ctx->_brute_chars;
}

inline uint64_t get_offset_basis(const context_t ctx) {
    return ctx->offset_basis;
}

inline fmpz* get_offset_basis_fmpz(const context_t ctx) {
    return (fmpz*)ctx->offset_basis_fmpz;
}

inline uint64_t get_prime(const context_t ctx) {
    return ctx->prime;
}

inline fmpz* get_prime_fmpz(const context_t ctx) {
    return (fmpz*)ctx->prime_fmpz;
}

/* debug */

void _fill_hex_char(char* out_buf, uint8_t c) {
    switch (c) {
        case 0x9:
            strcpy(out_buf, "\\t");
            break;
        case 0xa:
            strcpy(out_buf, "\\n");
            break;
        case 0xd:
            strcpy(out_buf, "\\r");
            break;
        case '\\':
            strcpy(out_buf, "\\\\");
            break;
        case '"':
            strcpy(out_buf, "\\\"");
            break;
        default:
        {
            if (0x20 <= c && c < 0x7f) {
                out_buf[0] = c;
                out_buf[1] = 0;
            }
            else {
                sprintf(out_buf, "\\x%02hhx", c);
            }
        }
    }
}

void print_context(context_t ctx) {
    char _tmp_buf[0x10] = {0};
    printf("**********************\n");
    printf("initialized=%s\n", is_initialized(ctx) ? "true" : "false");
    if (ctx->uses_fmpz) {
        printf("prime="); fmpz_print(get_prime_fmpz(ctx)); printf("\n");
        printf("offset_basis="); fmpz_print(get_offset_basis_fmpz(ctx)); printf("\n");
    }
    else {
        printf("prime=0x%lx\noffset_basis=0x%lx\n", get_prime(ctx), get_offset_basis(ctx));
    }
    printf("bits=%u\n", ctx->bits);
    printf("prefix=b\"");
    for (int i = 0; i < get_prefix(ctx)->length; ++i) {
        _fill_hex_char(_tmp_buf, (uint8_t)get_prefix(ctx)->data[i]);
        printf("%s", _tmp_buf);
    }
    printf("\"\nsuffix=b\"");
    for (int i = 0; i < get_suffix(ctx)->length; ++i) {
        _fill_hex_char(_tmp_buf, (uint8_t)get_suffix(ctx)->data[i]);
        printf("%s", _tmp_buf);
    }
    printf("\"\nbrute_chars=b\"");
    for (int i = 0; i < get_brute_chars(ctx)->length; ++i) {
        _fill_hex_char(_tmp_buf, (uint8_t)get_brute_chars(ctx)->data[i]);
        printf("%s", _tmp_buf);
    }
    printf("\"\nvalid_chars=b\"");
    for (int i = 0; i < 256; ++i) {
        if (ctx->valid_chars[i] == 1) {
            _fill_hex_char(_tmp_buf, (uint8_t)i);
            printf("%s", _tmp_buf);
        }
    }
    printf("\"\n**********************\n");
}