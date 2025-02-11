#include <iostream>
#include <random>
#include <string>
#include <algorithm>
#include <format>
#include <chrono>
#include <utility>

#if !defined(__cplusplus)
#   error "Why isnt __cplusplus defined"
#endif

#if __cplusplus >= 202302L  /* C++23 */
#include <print>
using namespace std::print;
#elif __cplusplus >= 202002L  /* C++20 */
// convenience function since we're using c++20, not c++23 (which has std::print)
template<typename... Args>
void print(std::string_view fmt, Args&&... args) {
    std::vformat_to(
        std::ostreambuf_iterator<char>(std::cout),
        fmt,
        std::make_format_args(args...)
    );
}
#else
#   error "You must be using at least C++20"
#endif

#include "crack.hpp"

std::string generate_random_string(uint32_t length, const std::string& charset) {
    std::random_device random_device;
    std::mt19937 generator(random_device());
    std::uniform_int_distribution<> distribution(0, charset.size() - 1);

    std::string random_string{};
    for (uint32_t i = 0; i < length; ++i) {
        random_string += charset[distribution(generator)];
    }
    return random_string;
}

std::vector<std::pair<uint64_t /* hash */, std::string /* orig str */>>
gen_random_hashes(
    uint32_t num_hashes,
    uint32_t str_length,
    const std::string charset = presets::ident,
    uint64_t offset = OFFSET_DEFAULT,
    uint64_t prime = PRIME_1b3
) {
    std::vector<std::pair<uint64_t, std::string>> ret{};
    ret.reserve(num_hashes);

    using FNV_t = FNVUtil<64>;

    for (uint32_t i = 0; i < num_hashes; ++i) {
        std::string rand_str = generate_random_string(str_length, charset);
        ret.emplace_back(std::make_pair(
            FNV_t::hash(rand_str, offset, prime),
            rand_str
        ));
    }

    return ret;
}

int main() {
    uint32_t total_cases = 0, success = 0, failed = 0, collision = 0;

    const auto rand_cases = gen_random_hashes(1000, 10, presets::ident);
    auto bm_start_time = std::chrono::system_clock::now();

    auto crack = CrackUtils<>(presets::ident);
    crack.set_bruting_charset(presets::ident);

    for (const auto& [hash, orig_str] : rand_cases) {
        std::string result;
        if (crack.brute_n(result, hash, 10) == HASH_CRACKED) {
            if (result == orig_str) {
                success++;
            } else {
                collision++;
            }
        } else {
            failed++;
        }

        total_cases++;
    }

    auto bm_end_time = std::chrono::system_clock::now();
    auto time_taken_ms = std::chrono::duration_cast<std::chrono::milliseconds>(bm_end_time - bm_start_time);

    print("--- RESULTS ---\n");
    print(" success   : {}\n", success);
    print(" failed    : {}\n", failed);
    print(" collision : {}\n", collision);
    print(" total     : {}\n", total_cases);
    print("\n");
    print("---- STATS ----\n");
    print(" valid hashes    : {:.2f}%\n", (success + collision) * 100.0 / total_cases);
    print(" correct hashes  : {:.2f}%\n", (success * 100.0) / total_cases);
    print(" time taken      : {:.3f}s\n", time_taken_ms.count() / 1000.0);
    print(" time taken/hash : {:.3f}s\n", time_taken_ms.count() / 1000.0 / total_cases);

    return 0;
}