#pragma once
#include <cstdint>
#include <iostream>

template <uint64_t num, uint64_t pow_mod>
class InverseHelper {
    // just to make sure
    static_assert(pow_mod <= 64, "The hard maximum on the pow_mod value is 64");

public:
    static constexpr uint64_t inverse();
};

template <uint64_t num>
class InverseHelper<num, 64> {
public:
    static constexpr uint64_t inverse();
};

#define __HELPER_TPP
#include "helpers.tpp"
#undef __HELPER_TPP