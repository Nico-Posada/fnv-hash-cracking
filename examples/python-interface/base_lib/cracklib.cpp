#include "cracklib.hpp"

CrackUtils_t crack;
bool has_init = false;

void init(const char* valid_chars, const char* bruting_chars) {
    crack = CrackUtils_t(valid_chars, bruting_chars);
    crack.set_suppress_false_positive_msg();
    has_init = true;
}

bool crack_hash(
    uint64_t hash,
    char* ret_buffer,
    size_t ret_buffer_size,
    uint32_t max_brute) {
    if (!has_init) {
        std::cerr << "Call init before running crack_hash!" << std::endl;
        return false;
    }

    std::string result;
    if (crack.brute_n(result, hash, max_brute) == HASH_CRACKED) {
        // safe string copy to not overflow
        snprintf(ret_buffer, ret_buffer_size, "%s", result.c_str());
        return true;
    }

    return false;
}