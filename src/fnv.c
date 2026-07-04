#include <string.h>
#include <stddef.h>

#include "fnv.h"

// internal version exists for the crack funcs to
// use directly with known lengths
uint64_t fnv_u64_with_len(char_buffer data, const uint64_t offset_basis, const uint64_t prime, const uint32_t bits) {
    uint64_t hash = offset_basis;
    for (size_t i = 0; i < data.length; ++i) {
        hash ^= (uint8_t)data.data[i];
        hash *= prime;
    }

    if (bits == 0) {
        return 0;
    }
    if (bits < 64) {
        hash &= ((uint64_t)1 << bits) - 1;
    }
    return hash;
}

uint64_t fnv_u64(const char* data, const uint64_t offset_basis, const uint64_t prime, const uint32_t bits) {
    return fnv_u64_with_len((char_buffer){data, strlen(data)}, offset_basis, prime, bits);
}

void fnv_fmpz_with_len_mask(
    fmpz_t result, char_buffer data, const fmpz_t offset_basis, const fmpz_t prime, const fmpz_t mask
) {
    if (result == prime || result == mask) {
        fmpz_t tmp;
        fmpz_init(tmp);
        fnv_fmpz_with_len_mask(tmp, data, offset_basis, prime, mask);
        fmpz_set(result, tmp);
        fmpz_clear(tmp);
        return;
    }

    fmpz_set(result, offset_basis);
    if (data.length == 0) {
        return;
    }

    fmpz_t cur_char;
    fmpz_init(cur_char);
    for (size_t i = 0; i < data.length; ++i) {
        // since fmpz values are normal ints until the number becomes larger than 2**62, we can
        // get away with making this fake fmpz value
        fmpz_set_ui(cur_char, (ulong)(uint8_t)data.data[i]);
        fmpz_xor(result, result, cur_char);
        fmpz_mul(result, result, prime);
        fmpz_and(result, result, mask);
    }

    fmpz_clear(cur_char);
}

void fnv_fmpz_with_len(
    fmpz_t result, char_buffer data, const fmpz_t offset_basis, const fmpz_t prime, const uint32_t bits
) {
    fmpz_t mask;
    fmpz_init(mask);
    fmpz_setbit(mask, bits);
    fmpz_sub_ui(mask, mask, (ulong)1);
    fnv_fmpz_with_len_mask(result, data, offset_basis, prime, mask);
    fmpz_clear(mask);
}

void fnv_fmpz(fmpz_t result, const char* data, const fmpz_t offset_basis, const fmpz_t prime, const uint32_t bits) {
    fnv_fmpz_with_len(result, (char_buffer){data, strlen(data)}, offset_basis, prime, bits);
}
