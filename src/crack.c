#include <stdio.h>

#include <flint/flint.h>
#include <flint/fmpz.h>
#include <flint/fmpz_mat.h>
#include <flint/fmpz_lll.h>

#include "crack.h"
#include "inverse.h"
#include "brute_gen.h"
#include "enumerate.h"
#include "fnv.h"

inline static CrackResult _check_prereqs(
    const context_t ctx,
    const uint32_t brute_len
) {
    if (fnvcrack_interrupted()) {
        return INTERRUPTED;
    }

    if (!is_initialized(ctx)) {
        return CONTEXT_UNINITIALIZED;
    }

    const char_buffer* brute_chars = get_brute_chars(ctx);
    if ((!brute_chars->data || !brute_chars->length) && brute_len > 0) {
        return MISSING_BRUTE_CHARS;
    }

    return SUCCESS;
}

inline static int32_t _check_resulting_matrix(
    const context_t ctx,
    const fmpz_mat_t M,
    const uint32_t dim,
    const uint64_t new_hash,
    const uint64_t prime,
    char* ret_buf
) {
    const uint32_t size = fmpz_mat_ncols(M);
    for (uint32_t i = 0; i < dim; ++i) {
        const slong row_last = fmpz_get_si(fmpz_mat_entry(M, i, size - 1));
        if (row_last != -1 && row_last != 1) {
            continue;
        }

        bool success = true;
        const int64_t lo_hsh = new_hash;
        const int64_t lo_p = prime;
        int64_t a = lo_hsh;

        for (uint32_t j = 1; j < size - 1; ++j) {
            const int64_t cur = fmpz_get_si(fmpz_mat_entry(M, i, j)) * row_last;

            const int64_t b = a;
            a += cur;
            const int64_t x = a ^ b;
            if (x >= 256 || ctx->valid_chars[x] == 0) {
                success = false;
                break;
            }

            ret_buf[j - 1] = x;
            a *= lo_p;
        }

        if (success) {
            return i;
        }
    }

    return -1;
}

inline static void _init_weights(
    uint64_t* Q,
    const uint32_t dim,
    const uint64_t start,
    const uint64_t mid,
    const uint64_t end
) {
    Q[0] = (uint64_t)1 << start; // 2 ^ start
    for (uint32_t i = 1; i < dim - 1; ++i) {
        Q[i] = (uint64_t)1 << mid; // 2 ^ mid
    }
    Q[dim - 1] = (uint64_t)1 << end; // 2 ^ end
}

inline static bool _store_result_safe(
    const char* prefix,
    const size_t prefix_len,
    const char* bruted,
    const size_t bruted_len,
    const char* cracked,
    const size_t cracked_len,
    const char* suffix,
    const size_t suffix_len,
    char** out_buffer,
    size_t* out_buffer_len
) {
    const size_t total_len = prefix_len + bruted_len + cracked_len + suffix_len;
    char* result_buf = malloc(total_len + 1);
    if (!result_buf) {
        return false;
    }

    size_t cur_off = 0;
    memcpy(result_buf + cur_off, prefix, prefix_len);
    cur_off += prefix_len;
    memcpy(result_buf + cur_off, bruted, bruted_len);
    cur_off += bruted_len;
    memcpy(result_buf + cur_off, cracked, cracked_len);
    cur_off += cracked_len;
    memcpy(result_buf + cur_off, suffix, suffix_len);
    cur_off += suffix_len;

    *out_buffer = result_buf;
    *out_buffer_len = total_len;
    return true;
}

inline static crack_options_t _normalize_options(const crack_options_t* options) {
    crack_options_t ret = {
        .strategy = CRACK_STRATEGY_LLL,
        .enum_bound = CRACK_DEFAULT_ENUM_BOUND,
        .max_enum_candidates = 0,
    };

    if (!options) {
        return ret;
    }

    ret = *options;
    return ret;
}

inline static uint64_t _bit_mask_u64(const uint32_t bit_len) {
    if (bit_len >= 64) {
        return ~(uint64_t)0;
    }
    return ((uint64_t)1 << bit_len) - 1;
}

inline static void _init_modulus(fmpz_t MOD, const uint32_t bit_len) {
    if (bit_len == 64) {
        fmpz_set_uiui(MOD, /*hi =*/(ulong)1, /*lo =*/(ulong)0);
    }
    else {
        fmpz_ui_pow_ui(MOD, 2, bit_len);
    }
}

inline static uint64_t _reverse_suffix_u64(
    uint64_t target,
    const char* suffix,
    const size_t suffix_len,
    const uint64_t prime,
    const uint32_t bit_len
) {
    if (suffix_len == 0) {
        return target;
    }

    const uint64_t inv_prime = inverse(prime, bit_len);
    for (int32_t i = suffix_len - 1; i >= 0; --i) {
        target *= inv_prime;
        target ^= (uint8_t)suffix[i];
        target &= _bit_mask_u64(bit_len);
    }
    return target;
}

inline static void _reverse_suffix_fmpz(
    fmpz_t ntarget,
    const fmpz_t target,
    const char* suffix,
    const size_t suffix_len,
    const fmpz_t prime,
    const fmpz_t bit_mask,
    const uint32_t bit_len
) {
    fmpz_set(ntarget, target);
    if (suffix_len == 0) {
        return;
    }

    fmpz_t inv_prime, cur_char;
    fmpz_init(inv_prime);
    fmpz_init(cur_char);

    inverse_fmpz(inv_prime, prime, bit_len);
    for (int32_t i = suffix_len - 1; i >= 0; --i) {
        fmpz_set_ui(cur_char, (ulong)(uint8_t)suffix[i]);
        fmpz_mul(ntarget, ntarget, inv_prime);
        fmpz_xor(ntarget, ntarget, cur_char);
        fmpz_and(ntarget, ntarget, bit_mask);
    }

    fmpz_clear(cur_char);
    fmpz_clear(inv_prime);
}

static void _delta_bounds(
    const context_t ctx,
    int64_t* lower_bounds,
    int64_t* upper_bounds,
    const uint32_t delta_len
) {
    int64_t lo = 255;
    int64_t hi = -255;

    for (uint32_t state = 0; state < 256; ++state) {
        for (uint32_t c = 0; c < 256; ++c) {
            if (!ctx->valid_chars[c]) {
                continue;
            }

            const int64_t delta = (int64_t)(state ^ c) - (int64_t)state;
            if (delta < lo) {
                lo = delta;
            }
            if (delta > hi) {
                hi = delta;
            }
        }
    }

    for (uint32_t i = 0; i < delta_len; ++i) {
        lower_bounds[i] = lo;
        upper_bounds[i] = hi;
    }
}

static void _set_first_delta_bounds(
    const context_t ctx,
    int64_t* lower_bounds,
    int64_t* upper_bounds,
    const uint32_t state_low
) {
    int64_t lo = 255;
    int64_t hi = -255;

    for (uint32_t c = 0; c < 256; ++c) {
        if (!ctx->valid_chars[c]) {
            continue;
        }

        const int64_t delta = (int64_t)(state_low ^ c) - (int64_t)state_low;
        if (delta < lo) {
            lo = delta;
        }
        if (delta > hi) {
            hi = delta;
        }
    }

    lower_bounds[0] = lo;
    upper_bounds[0] = hi;
}

static bool _deltas_to_bytes_u64(
    const context_t ctx,
    const uint64_t start_hash,
    const uint64_t prime,
    const uint32_t bit_len,
    const int64_t* deltas,
    const uint32_t delta_len,
    char* ret_buf
) {
    const uint64_t mask = _bit_mask_u64(bit_len);
    uint64_t state = start_hash & mask;

    for (uint32_t i = 0; i < delta_len; ++i) {
        const uint64_t next = (state + (uint64_t)deltas[i]) & mask;
        const uint64_t byte = next ^ state;
        if (byte >= 256 || !ctx->valid_chars[byte]) {
            return false;
        }

        ret_buf[i] = (char)byte;
        state = (next * prime) & mask;
    }

    return true;
}

static bool _deltas_to_bytes_fmpz(
    const context_t ctx,
    const fmpz_t start_hash,
    const fmpz_t prime,
    const fmpz_t modulus,
    const int64_t* deltas,
    const uint32_t delta_len,
    char* ret_buf
) {
    fmpz_t state, next, byte;
    fmpz_init_set(state, start_hash);
    fmpz_init(next);
    fmpz_init(byte);

    for (uint32_t i = 0; i < delta_len; ++i) {
        fmpz_set(next, state);
        fmpz_add_si(next, next, deltas[i]);
        if (fmpz_sgn(next) < 0 || fmpz_cmp(next, modulus) >= 0) {
            fmpz_clear(byte);
            fmpz_clear(next);
            fmpz_clear(state);
            return false;
        }

        fmpz_xor(byte, next, state);
        if (fmpz_cmp_ui(byte, (ulong)255) > 0) {
            fmpz_clear(byte);
            fmpz_clear(next);
            fmpz_clear(state);
            return false;
        }

        const uint64_t c = fmpz_get_ui(byte);
        if (!ctx->valid_chars[c]) {
            fmpz_clear(byte);
            fmpz_clear(next);
            fmpz_clear(state);
            return false;
        }

        ret_buf[i] = (char)c;
        fmpz_mul(state, next, prime);
        fmpz_mod(state, state, modulus);
    }

    fmpz_clear(byte);
    fmpz_clear(next);
    fmpz_clear(state);
    return true;
}

typedef struct {
    context_t ctx;
    uint64_t target;
    uint64_t new_hash;
    uint64_t prime;
    uint32_t bit_len;
    const char* prefix;
    size_t prefix_len;
    const char* bruted;
    size_t bruted_len;
    const char* suffix;
    size_t suffix_len;
    char_buffer* out_buffer;
    char* ret_buf;
    uint32_t delta_len;
    bool memory_error;
} enum_u64_cb_ctx_t;

static bool _enum_u64_candidate(
    const int64_t* deltas,
    uint32_t delta_len,
    void* userdata
) {
    enum_u64_cb_ctx_t* cb_ctx = userdata;
    if (!_deltas_to_bytes_u64(
        cb_ctx->ctx,
        cb_ctx->new_hash,
        cb_ctx->prime,
        cb_ctx->bit_len,
        deltas,
        delta_len,
        cb_ctx->ret_buf
    )) {
        return false;
    }

    uint64_t hash = fnv_u64_with_len(
        cb_ctx->ret_buf,
        cb_ctx->delta_len,
        cb_ctx->new_hash,
        cb_ctx->prime,
        cb_ctx->bit_len
    );
    hash = fnv_u64_with_len(
        cb_ctx->suffix,
        cb_ctx->suffix_len,
        hash,
        cb_ctx->prime,
        cb_ctx->bit_len
    );
    if (hash != cb_ctx->target) {
        return false;
    }

    char* output;
    size_t output_len;
    if (!_store_result_safe(
        cb_ctx->prefix, cb_ctx->prefix_len,
        cb_ctx->bruted, cb_ctx->bruted_len,
        cb_ctx->ret_buf, cb_ctx->delta_len,
        cb_ctx->suffix, cb_ctx->suffix_len,
        &output, &output_len
    )) {
        cb_ctx->memory_error = true;
        return true;
    }

    cb_ctx->out_buffer->data = output;
    cb_ctx->out_buffer->length = output_len;
    return true;
}

static CrackResult _crack_u64_with_len_enumerate(
    context_t ctx,
    uint64_t target,
    char_buffer* out_buffer,
    const uint32_t expected_len,
    const uint32_t brute_len,
    const crack_options_t* options
) {
    CrackResult prereq_chk = _check_prereqs(ctx, brute_len);
    if (prereq_chk != SUCCESS) {
        return prereq_chk;
    }

    const uint32_t bit_len = ctx->bits;
    const uint64_t prime = get_prime(ctx);
    const uint64_t offset_basis = get_offset_basis(ctx);
    const char* prefix = get_prefix(ctx)->data; if (!prefix) prefix = "";
    const char* suffix = get_suffix(ctx)->data; if (!suffix) suffix = "";
    char_buffer* brute_chars = get_brute_chars(ctx);
    const size_t prefix_len = get_prefix(ctx)->length;
    const size_t suffix_len = get_suffix(ctx)->length;

    if (expected_len < prefix_len + suffix_len + brute_len) {
        return BAD_SEARCH_LENGTH;
    }

    const uint32_t nn = expected_len - brute_len - prefix_len - suffix_len;
    crack_options_t opts = _normalize_options(options);

    brute_chars_t brute;
    if (!product(&brute, brute_chars->data, brute_chars->length, brute_len)) {
        return MEMORY_ERROR;
    }

    const uint64_t prefixed_hash = fnv_u64_with_len(prefix, prefix_len, offset_basis, prime, bit_len);
    const uint64_t ntarget = _reverse_suffix_u64(target, suffix, suffix_len, prime, bit_len);

    char* ret_buf = calloc((size_t)nn + 1, 1);
    int64_t* lower_bounds = NULL;
    int64_t* upper_bounds = NULL;
    fmpz_mat_t coeffs;
    fmpz_t MOD, rhs, new_hash_z, ntarget_z;
    fmpz_mat_init(coeffs, 1, nn);
    fmpz_init(MOD);
    fmpz_init(rhs);
    fmpz_init(new_hash_z);
    fmpz_init(ntarget_z);

    if (!ret_buf) {
        destroy_product(&brute);
        fmpz_mat_clear(coeffs);
        fmpz_clear(ntarget_z);
        fmpz_clear(new_hash_z);
        fmpz_clear(rhs);
        fmpz_clear(MOD);
        return MEMORY_ERROR;
    }

    CrackResult result = FAILED;
    _init_modulus(MOD, bit_len);
    fmpz_set_ui(ntarget_z, ntarget);

    if (nn != 0) {
        lower_bounds = malloc((size_t)nn * sizeof(*lower_bounds));
        upper_bounds = malloc((size_t)nn * sizeof(*upper_bounds));
        if (!lower_bounds || !upper_bounds) {
            result = MEMORY_ERROR;
            goto cleanup;
        }
        _delta_bounds(ctx, lower_bounds, upper_bounds, nn);

        for (uint32_t i = 0; i < nn; ++i) {
            fmpz_ui_pow_ui(fmpz_mat_entry(coeffs, 0, i), prime, nn - i);
        }
    }

    for (size_t i = 0; i < brute.total_entries; ++i) {
        if (fnvcrack_interrupted()) {
            result = INTERRUPTED;
            goto cleanup;
        }

        const char* cur = brute.buffer + i * brute.entry_length;
        const uint64_t new_hash = fnv_u64_with_len(cur, brute.entry_length, prefixed_hash, prime, bit_len);

        if (nn == 0) {
            uint64_t hash = fnv_u64_with_len(suffix, suffix_len, new_hash, prime, bit_len);
            if (hash != target) {
                continue;
            }

            char* output;
            size_t output_len;
            if (!_store_result_safe(
                prefix, prefix_len,
                cur, brute.entry_length,
                ret_buf, 0,
                suffix, suffix_len,
                &output, &output_len
            )) {
                result = MEMORY_ERROR;
                goto cleanup;
            }
            out_buffer->data = output;
            out_buffer->length = output_len;
            result = SUCCESS;
            goto cleanup;
        }

        fmpz_set_ui(new_hash_z, new_hash);
        fmpz_mul(rhs, new_hash_z, fmpz_mat_entry(coeffs, 0, 0));
        fmpz_sub(rhs, ntarget_z, rhs);
        fmpz_mod(rhs, rhs, MOD);
        _set_first_delta_bounds(ctx, lower_bounds, upper_bounds, new_hash & 0xff);

        enum_u64_cb_ctx_t cb_ctx = {
            .ctx = {ctx[0]},
            .target = target,
            .new_hash = new_hash,
            .prime = prime,
            .bit_len = bit_len,
            .prefix = prefix,
            .prefix_len = prefix_len,
            .bruted = cur,
            .bruted_len = brute.entry_length,
            .suffix = suffix,
            .suffix_len = suffix_len,
            .out_buffer = out_buffer,
            .ret_buf = ret_buf,
            .delta_len = nn,
            .memory_error = false,
        };

        enumerate_solver_result enum_result = enumerate_bounded_mod(
            coeffs,
            rhs,
            MOD,
            lower_bounds,
            upper_bounds,
            opts.enum_bound,
            opts.max_enum_candidates,
            _enum_u64_candidate,
            &cb_ctx
        );

        if (enum_result == ENUMERATE_SOLVER_INTERRUPTED) {
            result = INTERRUPTED;
            goto cleanup;
        }
        if (cb_ctx.memory_error || enum_result == ENUMERATE_SOLVER_MEMORY_ERROR) {
            result = MEMORY_ERROR;
            goto cleanup;
        }
        if (out_buffer->data) {
            result = SUCCESS;
            goto cleanup;
        }
    }

cleanup:
    free(upper_bounds);
    free(lower_bounds);
    free(ret_buf);
    fmpz_clear(ntarget_z);
    fmpz_clear(new_hash_z);
    fmpz_clear(rhs);
    fmpz_clear(MOD);
    fmpz_mat_clear(coeffs);
    destroy_product(&brute);
    return result;
}

typedef struct {
    context_t ctx;
    const fmpz* target;
    const fmpz* new_hash;
    const fmpz* prime;
    const fmpz* modulus;
    uint32_t bit_len;
    const char* prefix;
    size_t prefix_len;
    const char* bruted;
    size_t bruted_len;
    const char* suffix;
    size_t suffix_len;
    char_buffer* out_buffer;
    char* ret_buf;
    uint32_t delta_len;
    bool memory_error;
} enum_fmpz_cb_ctx_t;

static bool _enum_fmpz_candidate(
    const int64_t* deltas,
    uint32_t delta_len,
    void* userdata
) {
    enum_fmpz_cb_ctx_t* cb_ctx = userdata;
    if (!_deltas_to_bytes_fmpz(
        cb_ctx->ctx,
        cb_ctx->new_hash,
        cb_ctx->prime,
        cb_ctx->modulus,
        deltas,
        delta_len,
        cb_ctx->ret_buf
    )) {
        return false;
    }

    fmpz_t hash;
    fmpz_init(hash);
    fnv_fmpz_with_len(
        hash,
        cb_ctx->ret_buf,
        cb_ctx->delta_len,
        cb_ctx->new_hash,
        cb_ctx->prime,
        cb_ctx->bit_len
    );
    fnv_fmpz_with_len(
        hash,
        cb_ctx->suffix,
        cb_ctx->suffix_len,
        hash,
        cb_ctx->prime,
        cb_ctx->bit_len
    );

    const bool is_eq = fmpz_equal(hash, cb_ctx->target);
    fmpz_clear(hash);
    if (!is_eq) {
        return false;
    }

    char* output;
    size_t output_len;
    if (!_store_result_safe(
        cb_ctx->prefix, cb_ctx->prefix_len,
        cb_ctx->bruted, cb_ctx->bruted_len,
        cb_ctx->ret_buf, cb_ctx->delta_len,
        cb_ctx->suffix, cb_ctx->suffix_len,
        &output, &output_len
    )) {
        cb_ctx->memory_error = true;
        return true;
    }

    cb_ctx->out_buffer->data = output;
    cb_ctx->out_buffer->length = output_len;
    return true;
}

static CrackResult _crack_fmpz_with_len_enumerate(
    context_t ctx,
    fmpz_t target,
    char_buffer* out_buffer,
    const uint32_t expected_len,
    const uint32_t brute_len,
    const crack_options_t* options
) {
    CrackResult prereq_chk = _check_prereqs(ctx, brute_len);
    if (prereq_chk != SUCCESS) {
        return prereq_chk;
    }

    const uint32_t bit_len = ctx->bits;
    const fmpz* prime = (fmpz*)ctx->prime_fmpz;
    const fmpz* offset_basis = (fmpz*)ctx->offset_basis_fmpz;
    const char* prefix = get_prefix(ctx)->data; if (!prefix) prefix = "";
    const char* suffix = get_suffix(ctx)->data; if (!suffix) suffix = "";
    char_buffer* brute_chars = get_brute_chars(ctx);
    const size_t prefix_len = get_prefix(ctx)->length;
    const size_t suffix_len = get_suffix(ctx)->length;

    if (expected_len < prefix_len + suffix_len + brute_len) {
        return BAD_SEARCH_LENGTH;
    }

    const uint32_t nn = expected_len - brute_len - prefix_len - suffix_len;
    crack_options_t opts = _normalize_options(options);

    brute_chars_t brute;
    if (!product(&brute, brute_chars->data, brute_chars->length, brute_len)) {
        return MEMORY_ERROR;
    }

    char* ret_buf = calloc((size_t)nn + 1, 1);
    int64_t* lower_bounds = NULL;
    int64_t* upper_bounds = NULL;
    fmpz_mat_t coeffs;
    fmpz_t MOD, bit_mask, ntarget, prefixed_hash, new_hash, rhs;
    fmpz_mat_init(coeffs, 1, nn);
    fmpz_init(MOD);
    fmpz_init(bit_mask);
    fmpz_init(ntarget);
    fmpz_init(prefixed_hash);
    fmpz_init(new_hash);
    fmpz_init(rhs);

    if (!ret_buf) {
        destroy_product(&brute);
        fmpz_clear(rhs);
        fmpz_clear(new_hash);
        fmpz_clear(prefixed_hash);
        fmpz_clear(ntarget);
        fmpz_clear(bit_mask);
        fmpz_clear(MOD);
        fmpz_mat_clear(coeffs);
        return MEMORY_ERROR;
    }

    CrackResult result = FAILED;
    fmpz_ui_pow_ui(MOD, 2, bit_len);
    fmpz_sub_ui(bit_mask, MOD, (ulong)1);
    _reverse_suffix_fmpz(ntarget, target, suffix, suffix_len, prime, bit_mask, bit_len);
    fnv_fmpz_with_len(prefixed_hash, prefix, prefix_len, offset_basis, prime, bit_len);

    if (nn != 0) {
        lower_bounds = malloc((size_t)nn * sizeof(*lower_bounds));
        upper_bounds = malloc((size_t)nn * sizeof(*upper_bounds));
        if (!lower_bounds || !upper_bounds) {
            result = MEMORY_ERROR;
            goto cleanup;
        }
        _delta_bounds(ctx, lower_bounds, upper_bounds, nn);

        for (uint32_t i = 0; i < nn; ++i) {
            fmpz_pow_ui(fmpz_mat_entry(coeffs, 0, i), prime, (ulong)(nn - i));
        }
    }

    for (size_t i = 0; i < brute.total_entries; ++i) {
        if (fnvcrack_interrupted()) {
            result = INTERRUPTED;
            goto cleanup;
        }

        const char* cur = brute.buffer + i * brute.entry_length;
        fnv_fmpz_with_len(new_hash, cur, brute.entry_length, prefixed_hash, prime, bit_len);

        if (nn == 0) {
            fmpz_t hash;
            fmpz_init(hash);
            fnv_fmpz_with_len(hash, suffix, suffix_len, new_hash, prime, bit_len);
            const bool is_eq = fmpz_equal(hash, target);
            fmpz_clear(hash);
            if (!is_eq) {
                continue;
            }

            char* output;
            size_t output_len;
            if (!_store_result_safe(
                prefix, prefix_len,
                cur, brute.entry_length,
                ret_buf, 0,
                suffix, suffix_len,
                &output, &output_len
            )) {
                result = MEMORY_ERROR;
                goto cleanup;
            }
            out_buffer->data = output;
            out_buffer->length = output_len;
            result = SUCCESS;
            goto cleanup;
        }

        fmpz_mul(rhs, new_hash, fmpz_mat_entry(coeffs, 0, 0));
        fmpz_sub(rhs, ntarget, rhs);
        fmpz_mod(rhs, rhs, MOD);
        _set_first_delta_bounds(ctx, lower_bounds, upper_bounds, fmpz_fdiv_ui(new_hash, 256));

        enum_fmpz_cb_ctx_t cb_ctx = {
            .ctx = {ctx[0]},
            .target = target,
            .new_hash = new_hash,
            .prime = prime,
            .modulus = MOD,
            .bit_len = bit_len,
            .prefix = prefix,
            .prefix_len = prefix_len,
            .bruted = cur,
            .bruted_len = brute.entry_length,
            .suffix = suffix,
            .suffix_len = suffix_len,
            .out_buffer = out_buffer,
            .ret_buf = ret_buf,
            .delta_len = nn,
            .memory_error = false,
        };

        enumerate_solver_result enum_result = enumerate_bounded_mod(
            coeffs,
            rhs,
            MOD,
            lower_bounds,
            upper_bounds,
            opts.enum_bound,
            opts.max_enum_candidates,
            _enum_fmpz_candidate,
            &cb_ctx
        );

        if (enum_result == ENUMERATE_SOLVER_INTERRUPTED) {
            result = INTERRUPTED;
            goto cleanup;
        }
        if (cb_ctx.memory_error || enum_result == ENUMERATE_SOLVER_MEMORY_ERROR) {
            result = MEMORY_ERROR;
            goto cleanup;
        }
        if (out_buffer->data) {
            result = SUCCESS;
            goto cleanup;
        }
    }

cleanup:
    free(upper_bounds);
    free(lower_bounds);
    free(ret_buf);
    fmpz_clear(rhs);
    fmpz_clear(new_hash);
    fmpz_clear(prefixed_hash);
    fmpz_clear(ntarget);
    fmpz_clear(bit_mask);
    fmpz_clear(MOD);
    fmpz_mat_clear(coeffs);
    destroy_product(&brute);
    return result;
}

static CrackResult _crack_u64_with_len_lll(
    context_t ctx,
    uint64_t target,
    char_buffer* out_buffer,
    const uint32_t expected_len,
    const uint32_t brute_len
) {
    CrackResult prereq_chk = _check_prereqs(ctx, brute_len);
    if (prereq_chk != SUCCESS) {
        return prereq_chk;
    }

    // ctx vars that we access a lot and aren't changed
    register const uint32_t bit_len = ctx->bits;
    register const uint64_t prime = get_prime(ctx);
    register const uint64_t offset_basis = get_offset_basis(ctx);
    register const char* prefix = get_prefix(ctx)->data; if (!prefix) prefix = "";
    register const char* suffix = get_suffix(ctx)->data; if (!suffix) suffix = "";
    char_buffer* brute_chars = get_brute_chars(ctx);

    fmpz_t MOD; fmpz_init(MOD);
    if (bit_len == 64) {
        fmpz_set_uiui(MOD, /*hi =*/(ulong)1, /*lo =*/(ulong)0); // 2 ^ 64
    }
    else {
        fmpz_set_ui(MOD, (uint64_t)1 << bit_len); // 2 ^ bit_len
    }

    const size_t prefix_len = get_prefix(ctx)->length;
    const size_t suffix_len = get_suffix(ctx)->length;

    const uint32_t nn = expected_len - brute_len - prefix_len - suffix_len;
    const uint32_t dim = nn + 2;

    register const uint64_t bit_mask = ~(uint64_t)0 >> (64 - bit_len);

    uint64_t P = 1;
    for (uint32_t i = 0; i < nn; ++i) {
        P *= prime;
    }
    P &= bit_mask;

    // using VLA instead of malloc
    // TODO: allow the user to customize the weights
    uint64_t Q[dim];
    _init_weights(Q, dim, 12, 4, 10);

    // identity matrix but with an extra column on the left and extra row on the bottom
    fmpz_mat_t _M;
    fmpz_mat_init(_M, dim, dim);
    for (uint32_t i = 0; i <= nn; ++i) {
        fmpz_set_ui(fmpz_mat_entry(_M, i, i+1), (ulong)1);
    }

    // fill in extra column on the left
    // (except second to last val)
    for (uint32_t i = 0; i < nn; ++i) {
        fmpz_ui_pow_ui(fmpz_mat_entry(_M, i, 0), prime, nn - i);
    }
    fmpz_set(fmpz_mat_entry(_M, dim - 1, 0), MOD);

    // M *= Q (part 1)
    // this should be done on every iteration, but since we only change one element in the M matrix
    // on each iteration, we can precompute almost everything else
    for (uint32_t x = 0; x < dim; ++x) {
        const uint64_t Q_val = Q[x];
        for (uint32_t y = 0; y < dim; ++y) {
            fmpz* const data = fmpz_mat_entry(_M, y, x);
            fmpz_mul_ui(data, data, Q_val);
        }
    }

    // perform reverse of fnv algo to get hash without suffix applied
    uint64_t ntarget = target;
    if (suffix_len != 0) {
        uint64_t inv_prime = inverse(prime, bit_len);
        for (int32_t i = suffix_len - 1; i >= 0; --i) {
            ntarget *= inv_prime;
            ntarget ^= suffix[i];
        }
    }

    // temp buffer to store possible cracks
    char ret_buf[dim];
    memset(ret_buf, 0, dim);

    // matrix that will take data from _M
    fmpz_mat_t M;
    fmpz_mat_init(M, dim, dim);

    // LLL config used for every iteration
    fmpz_lll_t fl;
    fmpz_lll_context_init_default(fl);

    CrackResult result = FAILED;
    brute_chars_t brute = {0};
    if (!product(&brute, brute_chars->data, brute_chars->length, brute_len)) {
        result = MEMORY_ERROR;
        goto cleanup;
    }

    const uint64_t prefixed_hash = fnv_u64_with_len(prefix, prefix_len, offset_basis, prime, bit_len);
    for (size_t i = 0; i < brute.total_entries; ++i) {
        if (fnvcrack_interrupted()) {
            result = INTERRUPTED;
            goto cleanup;
        }

        const char* cur = brute.buffer + i * brute.entry_length;

        // get the hash without the prefix applied
        const uint64_t new_hash = fnv_u64_with_len(cur, brute.entry_length, prefixed_hash, prime, bit_len);

        const uint64_t m = (new_hash * P - ntarget) & bit_mask;

        // create copy with (0, dim - 2) set
        fmpz_mat_set(M, _M);
        fmpz_set_ui(fmpz_mat_entry(M, dim - 2, 0), m);

        // M *= Q (part 2)
        fmpz_mul_ui(fmpz_mat_entry(M, dim - 2, 0), fmpz_mat_entry(M, dim - 2, 0), Q[0]);

        // M = M.LLL()
        fmpz_lll_d(M, NULL, fl);
        if (fnvcrack_interrupted()) {
            result = INTERRUPTED;
            goto cleanup;
        }

        // M /= Q
        for (uint32_t x = 0; x < dim; ++x) {
            const uint64_t Q_val = Q[x];
            for (uint32_t y = 0; y < dim; ++y) {
                fmpz* const data = fmpz_mat_entry(M, y, x);
                fmpz_divexact_ui(data, data, Q_val);
            }
        }

        const int32_t idx = _check_resulting_matrix(
            ctx, M, dim, new_hash, prime, ret_buf
        );

        if (idx >= 0) {
            // confirm the hash is correct, false positives are possible
            uint64_t hash = fnv_u64_with_len(ret_buf, nn, new_hash, prime, bit_len);
            hash = fnv_u64_with_len(suffix, suffix_len, hash, prime, bit_len);

            if (hash == target) {
                char* output;
                size_t output_len;
                if (!_store_result_safe(
                    prefix, prefix_len,
                    cur, brute.entry_length,
                    ret_buf, nn,
                    suffix, suffix_len,
                    &output, &output_len)) {
                    result = MEMORY_ERROR;
                    goto cleanup;
                }

                out_buffer->data = output;
                out_buffer->length = output_len;
                result = SUCCESS;
                goto cleanup;
            }
        }
    }

cleanup:
    destroy_product(&brute);
    fmpz_mat_clear(_M);
    fmpz_mat_clear(M);
    fmpz_clear(MOD);
    return result;
}

///////////////////////////////////////////////////////
///////////////////////////////////////////////////////
///////////////////////////////////////////////////////

static CrackResult _crack_fmpz_with_len_lll(
    context_t ctx,
    fmpz_t target,
    char_buffer* out_buffer,
    const uint32_t expected_len,
    const uint32_t brute_len
) {
    CrackResult prereq_chk = _check_prereqs(ctx, brute_len);
    if (prereq_chk != SUCCESS) {
        return prereq_chk;
    }

    // ctx vars that we access a lot and aren't changed
    const uint32_t bit_len = ctx->bits;
    const fmpz* prime = (fmpz*)ctx->prime_fmpz;
    const fmpz* offset_basis = (fmpz*)ctx->offset_basis_fmpz;
    const char* prefix = get_prefix(ctx)->data; if (!prefix) prefix = "";
    const char* suffix = get_suffix(ctx)->data; if (!suffix) suffix = "";
    char_buffer* brute_chars = get_brute_chars(ctx);

    fmpz_t MOD; fmpz_init(MOD);
    fmpz_ui_pow_ui(MOD, 2, bit_len);

    const size_t prefix_len = get_prefix(ctx)->length;
    const size_t suffix_len = get_suffix(ctx)->length;

    const uint32_t nn = expected_len - brute_len - prefix_len - suffix_len;
    const uint32_t dim = nn + 2;

    // const uint64_t bit_mask = bit_len != 64 ? ((uint64_t)1 << bit_len) - 1 : ~(uint64_t)0;
    // const uint64_t bit_mask = ~(uint64_t)0 >> (64 - bit_len);
    fmpz_t bit_mask; fmpz_init(bit_mask);
    fmpz_sub_ui(bit_mask, MOD, (ulong)1);

    fmpz_t P; fmpz_init_set_ui(P, (ulong)1);
    for (uint32_t i = 0; i < nn; ++i) {
        fmpz_mul(P, P, prime);
        fmpz_and(P, P, bit_mask);
    }

    // using VLA instead of malloc
    // TODO: allow the user to customize the weights
    uint64_t Q[dim];
    _init_weights(Q, dim, 12, 4, 10);

    // identity matrix but with an extra column on the left and extra row on the bottom
    fmpz_mat_t _M;
    fmpz_mat_init(_M, dim, dim);
    for (uint32_t i = 0; i <= nn; ++i) {
        fmpz_set_ui(fmpz_mat_entry(_M, i, i+1), (ulong)1);
    }

    // fill in extra column on the left
    // (except second to last val)
    for (uint32_t i = 0; i < nn; ++i) {
        fmpz_pow_ui(fmpz_mat_entry(_M, i, 0), prime, (ulong)(nn - i));
    }
    fmpz_set(fmpz_mat_entry(_M, dim - 1, 0), MOD);

    // M *= Q (part 1)
    // this should be done on every iteration, but since we only change one element in the M matrix
    // on each iteration, we can precompute almost everything else
    for (uint32_t x = 0; x < dim; ++x) {
        const uint64_t Q_val = Q[x];
        for (uint32_t y = 0; y < dim; ++y) {
            fmpz* const data = fmpz_mat_entry(_M, y, x);
            fmpz_mul_ui(data, data, Q_val);
        }
    }

    // perform reverse of fnv algo to get hash without suffix applied
    fmpz_t ntarget;
    fmpz_init_set(ntarget, target);
    if (suffix_len != 0) {
        fmpz_t inv_prime, cur_char;
        fmpz_init(inv_prime); fmpz_init(cur_char);
        inverse_fmpz(inv_prime, prime, bit_len);
        for (int32_t i = suffix_len - 1; i >= 0; --i) {
            fmpz_set_ui(cur_char, (ulong)(uint8_t)suffix[i]);
            fmpz_mul(ntarget, ntarget, inv_prime);
            fmpz_xor(ntarget, ntarget, cur_char);
            fmpz_and(ntarget, ntarget, bit_mask);
        }
        fmpz_clear(inv_prime);
        fmpz_clear(cur_char);
    }

    char ret_buf[dim];
    memset(ret_buf, 0, dim);

    // matrix that will take data from _M
    fmpz_mat_t M;
    fmpz_mat_init(M, dim, dim);

    // LLL config used for every iteration
    fmpz_lll_t fl;
    fmpz_lll_context_init_default(fl);

    // precompute the hash with the prefix applied
    fmpz_t prefixed_hash;
    fmpz_init(prefixed_hash);
    fnv_fmpz_with_len(prefixed_hash, prefix, prefix_len, offset_basis, prime, bit_len);

    // vars used in the loop
    fmpz_t new_hash, m;
    fmpz_init(new_hash);
    fmpz_init(m);

    CrackResult result = FAILED;
    brute_chars_t brute = {0};
    if (!product(&brute, brute_chars->data, brute_chars->length, brute_len)) {
        result = MEMORY_ERROR;
        goto cleanup;
    }

    for (size_t i = 0; i < brute.total_entries; ++i) {
        if (fnvcrack_interrupted()) {
            result = INTERRUPTED;
            goto cleanup;
        }

        const char* cur = brute.buffer + i * brute.entry_length;

        // get the hash without the prefix applied
        fnv_fmpz_with_len(new_hash, cur, brute.entry_length, prefixed_hash, prime, bit_len);

        // const uint64_t m = (new_hash * P - ntarget) & bit_mask;
        fmpz_mul(m, new_hash, P);
        fmpz_sub(m, m, ntarget);
        fmpz_and(m, m, bit_mask);

        // create copy with (0, dim - 2) set
        fmpz_mat_set(M, _M);
        fmpz_set(fmpz_mat_entry(M, dim - 2, 0), m);

        // M *= Q (part 2)
        fmpz_mul_ui(fmpz_mat_entry(M, dim - 2, 0), fmpz_mat_entry(M, dim - 2, 0), Q[0]);

        // M = M.LLL()
        fmpz_lll_d(M, NULL, fl);
        if (fnvcrack_interrupted()) {
            result = INTERRUPTED;
            goto cleanup;
        }

        // M /= Q
        for (uint32_t x = 0; x < dim; ++x) {
            const uint64_t Q_val = Q[x];
            for (uint32_t y = 0; y < dim; ++y) {
                fmpz* const data = fmpz_mat_entry(M, y, x);
                fmpz_divexact_ui(data, data, Q_val);
            }
        }

        const int32_t idx = _check_resulting_matrix(
            ctx, M, dim, fmpz_get_ui(new_hash), fmpz_get_ui(prime), ret_buf
        );

        if (idx >= 0) {
            // confirm the hash is correct, false positives are possible
            fmpz_t hash; fmpz_init(hash);
            fnv_fmpz_with_len(hash, ret_buf, nn, new_hash, prime, bit_len);
            fnv_fmpz_with_len(hash, suffix, suffix_len, hash, prime, bit_len);

            const bool is_eq = fmpz_equal(hash, target);
            fmpz_clear(hash);

            if (is_eq) {
                char* output;
                size_t output_len;
                if (!_store_result_safe(
                    prefix, prefix_len,
                    cur, brute.entry_length,
                    ret_buf, nn,
                    suffix, suffix_len,
                    &output, &output_len)) {
                    result = MEMORY_ERROR;
                    goto cleanup;
                }

                out_buffer->data = output;
                out_buffer->length = output_len;
                result = SUCCESS;
                goto cleanup;
            }
        }
    }

cleanup:
    destroy_product(&brute);
    fmpz_mat_clear(_M);
    fmpz_mat_clear(M);
    fmpz_clear(MOD);
    fmpz_clear(m);
    fmpz_clear(new_hash);
    fmpz_clear(prefixed_hash);
    fmpz_clear(P);
    fmpz_clear(bit_mask);
    fmpz_clear(ntarget);
    return result;
}

CrackResult crack_u64_with_len_options(
    context_t ctx,
    uint64_t target,
    char_buffer* out_buffer,
    const uint32_t expected_len,
    const uint32_t brute_len,
    const crack_options_t* options
) {
    crack_options_t opts = _normalize_options(options);
    if (opts.strategy == CRACK_STRATEGY_ENUMERATE) {
        return _crack_u64_with_len_enumerate(ctx, target, out_buffer, expected_len, brute_len, &opts);
    }
    return _crack_u64_with_len_lll(ctx, target, out_buffer, expected_len, brute_len);
}

CrackResult crack_fmpz_with_len_options(
    context_t ctx,
    fmpz_t target,
    char_buffer* out_buffer,
    const uint32_t expected_len,
    const uint32_t brute_len,
    const crack_options_t* options
) {
    crack_options_t opts = _normalize_options(options);
    if (opts.strategy == CRACK_STRATEGY_ENUMERATE) {
        return _crack_fmpz_with_len_enumerate(ctx, target, out_buffer, expected_len, brute_len, &opts);
    }
    return _crack_fmpz_with_len_lll(ctx, target, out_buffer, expected_len, brute_len);
}

CrackResult crack_u64_with_len(
    context_t ctx,
    uint64_t target,
    char_buffer* out_buffer,
    const uint32_t expected_len,
    const uint32_t brute_len
) {
    return _crack_u64_with_len_lll(ctx, target, out_buffer, expected_len, brute_len);
}

CrackResult crack_fmpz_with_len(
    context_t ctx,
    fmpz_t target,
    char_buffer* out_buffer,
    const uint32_t expected_len,
    const uint32_t brute_len
) {
    return _crack_fmpz_with_len_lll(ctx, target, out_buffer, expected_len, brute_len);
}

CrackResult crack_u64_options(
    context_t ctx,
    const uint64_t target,
    char_buffer* out_buffer,
    const uint32_t max_search_len,
    const uint64_t max_crack_len,
    const crack_options_t* options
) {
    const uint32_t known_len = get_prefix(ctx)->length + get_suffix(ctx)->length;
    if (max_search_len == 0 && known_len == 0) {
        return BAD_SEARCH_LENGTH;
    }

    if (max_search_len == 0) {
        return crack_u64_with_len_options(ctx, target, out_buffer, known_len, 0, options);
    }

    for (uint32_t n = 1 + known_len; n <= max_search_len + known_len; ++n) {
        if (fnvcrack_interrupted()) {
            return INTERRUPTED;
        }

        const uint32_t brute_len = n <= max_crack_len + known_len ? 0 : n - known_len - max_crack_len;
        CrackResult ret = crack_u64_with_len_options(ctx, target, out_buffer, n, brute_len, options);
        if (ret == -1)
            continue;

        return ret; // can be an error
    }

    return FAILED;
}

CrackResult crack_fmpz_options(
    context_t ctx,
    fmpz_t target,
    char_buffer* out_buffer,
    const uint32_t max_search_len,
    const uint64_t max_crack_len,
    const crack_options_t* options
) {
    const uint32_t known_len = get_prefix(ctx)->length + get_suffix(ctx)->length;
    if (max_search_len == 0 && known_len == 0) {
        return BAD_SEARCH_LENGTH;
    }

    if (max_search_len == 0) {
        return crack_fmpz_with_len_options(ctx, target, out_buffer, known_len, 0, options);
    }

    for (uint32_t n = 1 + known_len; n <= max_search_len + known_len; ++n) {
        if (fnvcrack_interrupted()) {
            return INTERRUPTED;
        }

        const uint32_t brute_len = n <= max_crack_len + known_len ? 0 : n - known_len - max_crack_len;
        CrackResult ret = crack_fmpz_with_len_options(ctx, target, out_buffer, n, brute_len, options);
        if (ret == -1)
            continue;

        return ret; // can be an error
    }

    return FAILED;
}

CrackResult crack_u64(
    context_t ctx,
    const uint64_t target,
    char_buffer* out_buffer,
    const uint32_t max_search_len,
    const uint64_t max_crack_len
) {
    return crack_u64_options(ctx, target, out_buffer, max_search_len, max_crack_len, NULL);
}

CrackResult crack_fmpz(
    context_t ctx,
    fmpz_t target,
    char_buffer* out_buffer,
    const uint32_t max_search_len,
    const uint64_t max_crack_len
) {
    return crack_fmpz_options(ctx, target, out_buffer, max_search_len, max_crack_len, NULL);
}
