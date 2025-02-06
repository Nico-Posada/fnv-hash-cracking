#include "helpers.hpp"
#ifndef __HELPER_TPP
#   error "helpers.tpp is a template definition file for helpers.hpp, do not use it outside of that"
#endif

// all this for compile-time modular multiplicative inverse, thanks C++

static constexpr uint64_t _mulmod(uint64_t a, uint64_t b, uint64_t m) {
    uint64_t res = 0;
    
    while (a != 0) {
        if (a & 1) res = (res + b) % m;
        a >>= 1;
        b = (b << 1) % m;
    }

    return res;
}

template <uint64_t n>
static constexpr uint64_t _2pow64modn() {
    static_assert(n != 0, "Modulus by 0");
    
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

template <uint64_t n>
static constexpr uint64_t _2pow64divn() {
    static_assert(n != 0, "Division by 0");

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

template <uint64_t a, uint64_t b>
static constexpr std::tuple<uint64_t, uint64_t, uint64_t>
gcd_extended() {
    if constexpr (a == 0) {
        return std::make_tuple(b, 0UL, 1UL);
    } else {
        constexpr auto result = gcd_extended<b % a, a>();
        return std::make_tuple(
            std::get<0>(result),
            std::get<2>(result) - (b / a) * std::get<1>(result),
            std::get<1>(result)
        );
    }
}

template <uint64_t num, uint64_t pow>
constexpr uint64_t InverseHelper<num, pow>::inverse() {
    return std::get<1>(gcd_extended<num, 1ULL << pow>());
}

template <uint64_t num>
constexpr uint64_t InverseHelper<num, 64>::inverse() {
    if constexpr (num == 0) {
        return 0;
    } else {
        constexpr auto result = gcd_extended<_2pow64modn<num>(), num>();
        return std::get<2>(gcd_extended<_2pow64modn<num>(), num>()) - _2pow64divn<num>() * std::get<1>(result);
    }
}