#include <stdio.h>

#include <flint/flint.h>
#include <flint/fmpz.h>
#include <flint/fmpz_mat.h>
#include <flint/fmpz_lll.h>

#include "crack.h"
#include "inverse.h"
#include "brute_gen.h"
#include "fnv.h"

inline static CrackResult _check_prereqs(
    const context_t ctx,
    const uint32_t brute_len
) {
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
    cur_off += cracked_len;

    *out_buffer = result_buf;
    *out_buffer_len = total_len;
    return true;
}

CrackResult crack_u64_with_len(
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

    // brute chars
    brute_chars_t brute;
    product(&brute, brute_chars->data, brute_chars->length, brute_len);

    // var to store result to return
    CrackResult result = FAILED;

    const uint64_t prefixed_hash = fnv_u64_with_len(prefix, prefix_len, offset_basis, prime, bit_len);
    for (uint32_t i = 0; i < brute.total_entries; ++i) {
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

CrackResult crack_fmpz_with_len(
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
            fmpz_set_ui(cur_char, (ulong)suffix[i]);
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

    // brute chars
    brute_chars_t brute;
    product(&brute, brute_chars->data, brute_chars->length, brute_len);

    // var to store result to return
    CrackResult result = FAILED;

    // precompute the hash with the prefix applied
    fmpz_t prefixed_hash;
    fmpz_init(prefixed_hash);
    fnv_fmpz_with_len(prefixed_hash, prefix, prefix_len, offset_basis, prime, bit_len);

    // vars used in the loop
    fmpz_t new_hash, m;
    fmpz_init(new_hash);
    fmpz_init(m);

    for (uint32_t i = 0; i < brute.total_entries; ++i) {
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
    fmpz_clear(P);
    fmpz_clear(bit_mask);
    fmpz_clear(ntarget);
    return result;
}

CrackResult crack_u64(
    context_t ctx,
    const uint64_t target,
    char_buffer* out_buffer, 
    const uint32_t max_search_len,
    const uint64_t max_crack_len
) {
    if (max_search_len == 0) {
        return BAD_SEARCH_LENGTH;
    }

    const uint32_t known_len = get_prefix(ctx)->length + get_suffix(ctx)->length;
    for (uint32_t n = 1 + known_len; n <= max_search_len + known_len; ++n) {
        const uint32_t brute_len = n <= max_crack_len + known_len ? 0 : n - known_len - max_crack_len;
        CrackResult ret = crack_u64_with_len(ctx, target, out_buffer, n, brute_len);
        if (ret == -1)
            continue;

        return ret; // can be an error
    }

    return FAILED;
}

CrackResult crack_fmpz(
    context_t ctx,
    fmpz_t target,
    char_buffer* out_buffer,
    const uint32_t max_search_len,
    const uint64_t max_crack_len
) {
    if (max_search_len == 0) {
        return BAD_SEARCH_LENGTH;
    }

    const uint32_t known_len = get_prefix(ctx)->length + get_suffix(ctx)->length;
    for (uint32_t n = 1 + known_len; n <= max_search_len + known_len; ++n) {
        const uint32_t brute_len = n <= max_crack_len + known_len ? 0 : n - known_len - max_crack_len;
        CrackResult ret = crack_fmpz_with_len(ctx, target, out_buffer, n, brute_len);
        if (ret == -1)
            continue;

        return ret; // can be an error
    }

    return FAILED;
}