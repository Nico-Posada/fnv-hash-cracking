#include <stdio.h>
#include <stdlib.h>
#include "inverse.h"

uint64_t _mulmod(uint64_t a, uint64_t b, uint64_t m) {
    uint64_t res = 0;

    while (a != 0) {
        if (a & 1)
            res = (res + b) % m;
        a >>= 1;
        b = (b << 1) % m;
    }

    return res;
}

uint64_t _2pow64modn(uint64_t n) {
    if (n == 0) {
        fprintf(stderr, "FATAL: Modulus by 0 in _2pow64modn\n");
        exit(2);
    }

    uint64_t result = 1;
    uint64_t base = 2;
    int64_t exp = 64;
    base %= n;

    while (exp > 0) {
        if (exp & 1)
            result = (result * base) % n;
        base = _mulmod(base, base, n);
        exp >>= 1;
    }

    return result;
}

uint64_t _2pow64divn(uint64_t n) {
    if (n == 0) {
        fprintf(stderr, "FATAL: Division by 0 in _2pow64divn\n");
        exit(2);
    }

    uint64_t result = 0;
    uint64_t remainder = 0;
    uint64_t divisor = n;

    for (int32_t shift = 63; shift >= 0; --shift) {
        remainder = (remainder << 1) | 1;

        if (remainder >= divisor) {
            remainder -= divisor;
            result |= 1UL << shift;
        }
    }

    return result;
}

void _gcd_extended(uint64_t out_vals[2], uint64_t a, uint64_t b) {
    if (a == 0) {
        out_vals[0] = 0;
        out_vals[1] = 1;
        return;
    }

    uint64_t new_vals[2];
    _gcd_extended(new_vals, b % a, a);

    out_vals[0] = new_vals[1] - (b / a) * new_vals[0];
    out_vals[1] = new_vals[0];
}

// return pow(num, -1, 2**mod_exponent)
uint64_t inverse(uint64_t num, uint32_t mod_exponent) {
    uint64_t out_vals[2];
    if (num == 0) {
        return 0;
    } else if (mod_exponent == 64) {
        _gcd_extended(out_vals, _2pow64modn(num), num);
        return out_vals[1] - _2pow64divn(num) * out_vals[0];
    } else {
        const uint64_t mod = (uint64_t)1 << mod_exponent;
        _gcd_extended(out_vals, num, mod);
        return out_vals[0] & (mod - 1);
    }
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
