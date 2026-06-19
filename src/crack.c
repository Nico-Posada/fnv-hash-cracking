#include <stdio.h>

#include <flint/flint.h>
#include <flint/fmpz.h>
#include <flint/fmpz_mat.h>

#include "crack.h"
#include "inverse.h"
#include "enumerate.h"
#include "fnv.h"

inline static CrackResult _check_prereqs(const context_t ctx) {
    if (fnvcrack_interrupted()) {
        return INTERRUPTED;
    }

    if (!is_initialized(ctx)) {
        return CONTEXT_UNINITIALIZED;
    }

    return SUCCESS;
}

inline static bool _store_result_safe(
    char_buffer prefix,
    char_buffer cracked,
    char_buffer suffix,
    char_buffer* out_buffer
) {
    const size_t total_len = prefix.length + cracked.length + suffix.length;
    char* result_buf = malloc(total_len + 1);
    if (!result_buf) {
        return false;
    }

    size_t cur_off = 0;
    if (prefix.length) {
        memcpy(result_buf + cur_off, prefix.data, prefix.length);
        cur_off += prefix.length;
    }
    if (cracked.length) {
        memcpy(result_buf + cur_off, cracked.data, cracked.length);
        cur_off += cracked.length;
    }
    if (suffix.length) {
        memcpy(result_buf + cur_off, suffix.data, suffix.length);
        cur_off += suffix.length;
    }

    result_buf[cur_off] = 0;
    out_buffer->data = result_buf;
    out_buffer->length = total_len;
    return true;
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
    char_buffer suffix,
    const uint64_t prime,
    const uint32_t bit_len
) {
    if (suffix.length == 0) {
        return target;
    }

    const uint64_t inv_prime = inverse(prime, bit_len);
    for (size_t i = suffix.length; i > 0; --i) {
        target *= inv_prime;
        target ^= (uint8_t)suffix.data[i - 1];
        target &= _bit_mask_u64(bit_len);
    }
    return target;
}

inline static void _reverse_suffix_fmpz(
    fmpz_t ntarget,
    const fmpz_t target,
    char_buffer suffix,
    const fmpz_t prime,
    const fmpz_t bit_mask,
    const uint32_t bit_len
) {
    fmpz_set(ntarget, target);
    if (suffix.length == 0) {
        return;
    }

    fmpz_t inv_prime, cur_char;
    fmpz_init(inv_prime);
    fmpz_init(cur_char);

    inverse_fmpz(inv_prime, prime, bit_len);
    for (size_t i = suffix.length; i > 0; --i) {
        fmpz_set_ui(cur_char, (ulong)(uint8_t)suffix.data[i - 1]);
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
    char_buffer prefix;
    char_buffer suffix;
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
        (char_buffer){cb_ctx->ret_buf, cb_ctx->delta_len},
        cb_ctx->new_hash,
        cb_ctx->prime,
        cb_ctx->bit_len
    );
    hash = fnv_u64_with_len(
        cb_ctx->suffix,
        hash,
        cb_ctx->prime,
        cb_ctx->bit_len
    );
    if (hash != cb_ctx->target) {
        return false;
    }

    if (!_store_result_safe(
        cb_ctx->prefix,
        (char_buffer){cb_ctx->ret_buf, cb_ctx->delta_len},
        cb_ctx->suffix,
        cb_ctx->out_buffer
    )) {
        cb_ctx->memory_error = true;
        return true;
    }

    return true;
}

static CrackResult _crack_u64_with_len_enumerate(
    context_t ctx,
    uint64_t target,
    char_buffer* out_buffer,
    const uint32_t expected_len,
    const uint32_t enum_bound,
    const uint64_t max_enum_candidates
) {
    CrackResult prereq_chk = _check_prereqs(ctx);
    if (prereq_chk != SUCCESS) {
        return prereq_chk;
    }

    const uint32_t bit_len = ctx->bits;
    const uint64_t prime = get_prime(ctx);
    const uint64_t offset_basis = get_offset_basis(ctx);
    char_buffer prefix = *get_prefix(ctx);
    char_buffer suffix = *get_suffix(ctx);

    if (expected_len < prefix.length + suffix.length) {
        return BAD_SEARCH_LENGTH;
    }

    const uint32_t nn = expected_len - prefix.length - suffix.length;

    const uint64_t new_hash = fnv_u64_with_len(prefix, offset_basis, prime, bit_len);
    const uint64_t ntarget = _reverse_suffix_u64(target, suffix, prime, bit_len);

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

    if (fnvcrack_interrupted()) {
        result = INTERRUPTED;
        goto cleanup;
    }

    if (nn == 0) {
        uint64_t hash = fnv_u64_with_len(suffix, new_hash, prime, bit_len);
        if (hash != target) {
            goto cleanup;
        }

        if (!_store_result_safe(
            prefix,
            (char_buffer){ret_buf, 0},
            suffix,
            out_buffer
        )) {
            result = MEMORY_ERROR;
            goto cleanup;
        }
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
        .suffix = suffix,
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
        enum_bound,
        max_enum_candidates,
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

cleanup:
    free(upper_bounds);
    free(lower_bounds);
    free(ret_buf);
    fmpz_clear(ntarget_z);
    fmpz_clear(new_hash_z);
    fmpz_clear(rhs);
    fmpz_clear(MOD);
    fmpz_mat_clear(coeffs);
    return result;
}

typedef struct {
    context_t ctx;
    const fmpz* target;
    const fmpz* new_hash;
    const fmpz* prime;
    const fmpz* modulus;
    uint32_t bit_len;
    char_buffer prefix;
    char_buffer suffix;
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
        (char_buffer){cb_ctx->ret_buf, cb_ctx->delta_len},
        cb_ctx->new_hash,
        cb_ctx->prime,
        cb_ctx->bit_len
    );
    fnv_fmpz_with_len(
        hash,
        cb_ctx->suffix,
        hash,
        cb_ctx->prime,
        cb_ctx->bit_len
    );

    const bool is_eq = fmpz_equal(hash, cb_ctx->target);
    fmpz_clear(hash);
    if (!is_eq) {
        return false;
    }

    if (!_store_result_safe(
        cb_ctx->prefix,
        (char_buffer){cb_ctx->ret_buf, cb_ctx->delta_len},
        cb_ctx->suffix,
        cb_ctx->out_buffer
    )) {
        cb_ctx->memory_error = true;
        return true;
    }

    return true;
}

static CrackResult _crack_fmpz_with_len_enumerate(
    context_t ctx,
    fmpz_t target,
    char_buffer* out_buffer,
    const uint32_t expected_len,
    const uint32_t enum_bound,
    const uint64_t max_enum_candidates
) {
    CrackResult prereq_chk = _check_prereqs(ctx);
    if (prereq_chk != SUCCESS) {
        return prereq_chk;
    }

    const uint32_t bit_len = ctx->bits;
    const fmpz* prime = (fmpz*)ctx->prime_fmpz;
    const fmpz* offset_basis = (fmpz*)ctx->offset_basis_fmpz;
    char_buffer prefix = *get_prefix(ctx);
    char_buffer suffix = *get_suffix(ctx);

    if (expected_len < prefix.length + suffix.length) {
        return BAD_SEARCH_LENGTH;
    }

    const uint32_t nn = expected_len - prefix.length - suffix.length;

    char* ret_buf = calloc((size_t)nn + 1, 1);
    int64_t* lower_bounds = NULL;
    int64_t* upper_bounds = NULL;
    fmpz_mat_t coeffs;
    fmpz_t MOD, bit_mask, ntarget, new_hash, rhs;
    fmpz_mat_init(coeffs, 1, nn);
    fmpz_init(MOD);
    fmpz_init(bit_mask);
    fmpz_init(ntarget);
    fmpz_init(new_hash);
    fmpz_init(rhs);

    if (!ret_buf) {
        fmpz_clear(rhs);
        fmpz_clear(new_hash);
        fmpz_clear(ntarget);
        fmpz_clear(bit_mask);
        fmpz_clear(MOD);
        fmpz_mat_clear(coeffs);
        return MEMORY_ERROR;
    }

    CrackResult result = FAILED;
    fmpz_ui_pow_ui(MOD, 2, bit_len);
    fmpz_sub_ui(bit_mask, MOD, (ulong)1);
    _reverse_suffix_fmpz(ntarget, target, suffix, prime, bit_mask, bit_len);

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

    if (fnvcrack_interrupted()) {
        result = INTERRUPTED;
        goto cleanup;
    }

    fnv_fmpz_with_len(new_hash, prefix, offset_basis, prime, bit_len);

    if (nn == 0) {
        fmpz_t hash;
        fmpz_init(hash);
        fnv_fmpz_with_len(hash, suffix, new_hash, prime, bit_len);
        const bool is_eq = fmpz_equal(hash, target);
        fmpz_clear(hash);
        if (!is_eq) {
            goto cleanup;
        }

        if (!_store_result_safe(
            prefix,
            (char_buffer){ret_buf, 0},
            suffix,
            out_buffer
        )) {
            result = MEMORY_ERROR;
            goto cleanup;
        }
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
        .suffix = suffix,
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
        enum_bound,
        max_enum_candidates,
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

cleanup:
    free(upper_bounds);
    free(lower_bounds);
    free(ret_buf);
    fmpz_clear(rhs);
    fmpz_clear(new_hash);
    fmpz_clear(ntarget);
    fmpz_clear(bit_mask);
    fmpz_clear(MOD);
    fmpz_mat_clear(coeffs);
    return result;
}

CrackResult crack_u64_with_len_limits(
    context_t ctx,
    uint64_t target,
    char_buffer* out_buffer,
    const uint32_t expected_len,
    const uint32_t enum_bound,
    const uint64_t max_enum_candidates
) {
    return _crack_u64_with_len_enumerate(
        ctx,
        target,
        out_buffer,
        expected_len,
        enum_bound,
        max_enum_candidates
    );
}

CrackResult crack_fmpz_with_len_limits(
    context_t ctx,
    fmpz_t target,
    char_buffer* out_buffer,
    const uint32_t expected_len,
    const uint32_t enum_bound,
    const uint64_t max_enum_candidates
) {
    return _crack_fmpz_with_len_enumerate(
        ctx,
        target,
        out_buffer,
        expected_len,
        enum_bound,
        max_enum_candidates
    );
}

CrackResult crack_u64_with_len(
    context_t ctx,
    uint64_t target,
    char_buffer* out_buffer,
    const uint32_t expected_len
) {
    return crack_u64_with_len_limits(
        ctx,
        target,
        out_buffer,
        expected_len,
        CRACK_DEFAULT_ENUM_BOUND,
        CRACK_DEFAULT_MAX_ENUM_CANDIDATES
    );
}

CrackResult crack_fmpz_with_len(
    context_t ctx,
    fmpz_t target,
    char_buffer* out_buffer,
    const uint32_t expected_len
) {
    return crack_fmpz_with_len_limits(
        ctx,
        target,
        out_buffer,
        expected_len,
        CRACK_DEFAULT_ENUM_BOUND,
        CRACK_DEFAULT_MAX_ENUM_CANDIDATES
    );
}

CrackResult crack_u64_limits(
    context_t ctx,
    const uint64_t target,
    char_buffer* out_buffer,
    const uint32_t max_search_len,
    const uint32_t enum_bound,
    const uint64_t max_enum_candidates
) {
    const uint32_t known_len = get_prefix(ctx)->length + get_suffix(ctx)->length;
    if (max_search_len == 0 && known_len == 0) {
        return BAD_SEARCH_LENGTH;
    }

    if (max_search_len == 0) {
        return crack_u64_with_len_limits(
            ctx,
            target,
            out_buffer,
            known_len,
            enum_bound,
            max_enum_candidates
        );
    }

    for (uint32_t n = 1 + known_len; n <= max_search_len + known_len; ++n) {
        if (fnvcrack_interrupted()) {
            return INTERRUPTED;
        }

        CrackResult ret = crack_u64_with_len_limits(
            ctx,
            target,
            out_buffer,
            n,
            enum_bound,
            max_enum_candidates
        );
        if (ret == -1)
            continue;

        return ret; // can be an error
    }

    return FAILED;
}

CrackResult crack_fmpz_limits(
    context_t ctx,
    fmpz_t target,
    char_buffer* out_buffer,
    const uint32_t max_search_len,
    const uint32_t enum_bound,
    const uint64_t max_enum_candidates
) {
    const uint32_t known_len = get_prefix(ctx)->length + get_suffix(ctx)->length;
    if (max_search_len == 0 && known_len == 0) {
        return BAD_SEARCH_LENGTH;
    }

    if (max_search_len == 0) {
        return crack_fmpz_with_len_limits(
            ctx,
            target,
            out_buffer,
            known_len,
            enum_bound,
            max_enum_candidates
        );
    }

    for (uint32_t n = 1 + known_len; n <= max_search_len + known_len; ++n) {
        if (fnvcrack_interrupted()) {
            return INTERRUPTED;
        }

        CrackResult ret = crack_fmpz_with_len_limits(
            ctx,
            target,
            out_buffer,
            n,
            enum_bound,
            max_enum_candidates
        );
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
    const uint32_t max_search_len
) {
    return crack_u64_limits(
        ctx,
        target,
        out_buffer,
        max_search_len,
        CRACK_DEFAULT_ENUM_BOUND,
        CRACK_DEFAULT_MAX_ENUM_CANDIDATES
    );
}

CrackResult crack_fmpz(
    context_t ctx,
    fmpz_t target,
    char_buffer* out_buffer,
    const uint32_t max_search_len
) {
    return crack_fmpz_limits(
        ctx,
        target,
        out_buffer,
        max_search_len,
        CRACK_DEFAULT_ENUM_BOUND,
        CRACK_DEFAULT_MAX_ENUM_CANDIDATES
    );
}
