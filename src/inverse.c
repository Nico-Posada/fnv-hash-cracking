#include "inverse.h"

// return pow(num, -1, 2**mod_exponent)
uint64_t inverse(uint64_t num, uint32_t mod_exponent) {
    if (num == 0 || (num & 1) == 0 || mod_exponent == 0) {
        return 0;
    }

    uint64_t result = num;
    for (uint32_t i = 0; i < 6; ++i) {
        result *= 2 - num * result;
    }

    if (mod_exponent >= 64) {
        return result;
    }
    return result & (((uint64_t)1 << mod_exponent) - 1);
}

void inverse_fmpz(fmpz_t result, const fmpz_t num, const uint32_t mod_exponent) {
    fmpz_t MOD;
    fmpz_init(MOD);
    fmpz_ui_pow_ui(MOD, 2, mod_exponent);
    if (!fmpz_invmod(result, num, MOD)) {
        fmpz_zero(result);
    }
    fmpz_clear(MOD);
}
