#include <string.h>
#include <stddef.h>

#include "fnv.h"

// internal version exists for the crack funcs to
// use directly with known lengths
uint64_t fnv_u64_with_len(
    const char* data,
    const size_t data_len,
    const uint64_t offset_basis,
    const uint64_t prime,
    const uint32_t bits
) {
    uint64_t hash = offset_basis;
    for (size_t i = 0; i < data_len; ++i) {
        hash ^= (uint8_t)data[i];
        hash *= prime;
    }

    hash &= ~(uint64_t)0 >> (64 - bits);
    return hash;
}

uint64_t fnv_u64(
    const char* data,
    const uint64_t offset_basis,
    const uint64_t prime,
    const uint32_t bits
) {
    return fnv_u64_with_len(data, strlen(data), offset_basis, prime, bits);
}

// MUST CALL `fmpz_clear` ON THE RETURN VALUE AFTER YOU USE IT
void fnv_fmpz_with_len(
    fmpz_t result,
    const char* data,
    const size_t data_len,
    const fmpz_t offset_basis,
    const fmpz_t prime,
    const uint32_t bits
) {
    fmpz_t hash, mask, cur_char;
    fmpz_init_set(hash, offset_basis);
    fmpz_init(mask); fmpz_init(cur_char);
    fmpz_ui_pow_ui(mask, 2, bits);
    fmpz_sub_ui(mask, mask, (ulong)1);

    for (size_t i = 0; i < data_len; ++i) {
        // since fmpz values are normal ints until the number becomes larger than 2**62, we can
        // get away with making this fake fmpz value
        fmpz_set_ui(cur_char, (ulong)data[i]);
        fmpz_xor(hash, hash, cur_char);
        fmpz_mul(hash, hash, prime);
        fmpz_and(hash, hash, mask);
    }

    fmpz_set(result, hash);
    fmpz_clear(cur_char);
    fmpz_clear(mask);
    fmpz_clear(hash);
}

void fnv_fmpz(
    fmpz_t result,
    const char* data,
    const fmpz_t offset_basis,
    const fmpz_t prime,
    const uint32_t bits
) {
    fnv_fmpz_with_len(result, data, strlen(data), offset_basis, prime, bits);
}