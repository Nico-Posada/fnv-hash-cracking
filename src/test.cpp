#include <iostream>
#include "crack.hpp"

int main() {
    using FNV_t = FNVUtil<>;
    using CrackUtils_t = CrackUtils<>;

    auto crack = CrackUtils_t();

    uint64_t hashed = FNV_t::hash("lmfaololol");
    string result;
    if (crack.try_crack_single(result, hashed, 10, 2, "", "")) {
        printf("Found! %s\n", result.c_str());
    } else {
        printf("Failed ):\n");
    }

    return 0;
}