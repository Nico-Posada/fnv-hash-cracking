#include <stdio.h>
#include <stdlib.h>
#include "inverse.h"

uint64_t _mulmod(uint64_t a, uint64_t b, uint64_t m) {
    uint64_t res = 0;
    
    while (a != 0) {
        if (a & 1) res = (res + b) % m;
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
        if (exp & 1) result = (result * base) % n;
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
    }
    else if (mod_exponent == 64) {
        _gcd_extended(out_vals, _2pow64modn(num), num);
        return out_vals[1] - _2pow64divn(num) * out_vals[0];
    }
    else {
        const uint64_t mod = (uint64_t)1 << mod_exponent;
        _gcd_extended(out_vals, num, mod);
        return out_vals[0] & (mod - 1);
    }
}

/* fmpz variant */
void _gcd_extended_fmpz(fmpz out_vals[2], const fmpz_t a, const fmpz_t b) {
    if (fmpz_equal_ui(a, (ulong)0)) {
        fmpz_set_ui(&out_vals[0], (ulong)0);
        fmpz_set_ui(&out_vals[1], (ulong)1);
        return;
    }

    fmpz new_vals[2];
    fmpz_t tmp;
    fmpz_init(&new_vals[0]);
    fmpz_init(&new_vals[1]);
    fmpz_init(tmp);
    fmpz_mod(tmp, b, a);
    _gcd_extended_fmpz(new_vals, tmp, a);

    // x = y1 - (b//a) * x1
    fmpz_fdiv_q(tmp, b, a);
    fmpz_mul(tmp, tmp, &new_vals[0]);
    fmpz_sub(tmp, &new_vals[1], tmp);
    fmpz_set(&out_vals[0], tmp);
    // y = x1
    fmpz_set(&out_vals[1], &new_vals[0]);
    
    fmpz_clear(&new_vals[0]);
    fmpz_clear(&new_vals[1]);
    fmpz_clear(tmp);
}

void inverse_fmpz(fmpz_t result, const fmpz_t num, const uint32_t mod_exponent) {
    fmpz out_vals[2];
    fmpz_t MOD;
    fmpz_init(&out_vals[0]);
    fmpz_init(&out_vals[1]);
    fmpz_init(MOD);
    fmpz_ui_pow_ui(MOD, 2, mod_exponent);
    _gcd_extended_fmpz(out_vals, num, MOD);
    fmpz_mod(result, &out_vals[0], MOD);
    fmpz_clear(&out_vals[0]);
    fmpz_clear(&out_vals[1]);
    fmpz_clear(MOD);
}