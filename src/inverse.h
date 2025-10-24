/*
    File used for misc helper functions/structs that I may need
*/

#pragma once
#include <stdint.h>
#include <flint/fmpz.h>

/* used for slightly faster computation instead of using mpz */
uint64_t inverse(uint64_t num, uint32_t mod_exponent);

/* mpz variants */
void inverse_fmpz(fmpz_t result, const fmpz_t num, const uint32_t mod_exponent);