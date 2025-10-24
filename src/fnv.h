#pragma once
#include <stdint.h>
#include <flint/fmpz.h>

uint64_t fnv_u64_with_len(
    const char* data,
    const size_t data_len,
    const uint64_t offset_basis,
    const uint64_t prime,
    const uint32_t bits
);

uint64_t fnv_u64(
    const char* data,
    const uint64_t offset_basis,
    const uint64_t prime,
    const uint32_t bits
);

void fnv_fmpz_with_len(
    fmpz_t result,
    const char* data,
    const size_t data_len,
    const fmpz_t offset_basis,
    const fmpz_t prime,
    const uint32_t bits
);

void fnv_fmpz(
    fmpz_t result,
    const char* data,
    const fmpz_t offset_basis,
    const fmpz_t prime,
    const uint32_t bits
);