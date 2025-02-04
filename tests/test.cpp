#include <iostream>
#include <format>

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
        std::make_format_args(std::forward<Args>(args)...)
    );
}
#else
#   error "You must be using at least C++20"
#endif

#include "crack.hpp"

// using a known initial string just to test
void example_1() {
    print("--- EXAMPLE 1 ---\n");
    using FNV_t = FNVUtilStatic<>;
    using CrackUtils_t = CrackUtils<>;

    // setup
    auto crack = CrackUtils_t();
    string to_hash = "known/crackmelol.exe";
    uint64_t hashed = FNV_t::hash(to_hash);

    // var to store cracked string if found
    string result;

    // max string length without needing to brute force is 8
    // since the string we want to crack is of length 10, we need 2 chars of brute force
    constexpr int BRUTE_CHARS = 2;

    // adding known prefix and suffix (useful if you're trying to crack a hashed filename)
    const string known_prefix = "known/";
    const string known_suffix = ".exe";
    
    // crack
    print("Trying to crack: {:#x}\n", hashed);
    if (crack.try_crack_single(result, hashed, to_hash.size(), BRUTE_CHARS, known_prefix, known_suffix)) {
        print("Found! {}\n", result);
    } else {
        print("Failed ):\n");
    }

    print("\n");
}

// example of cracking hash without knowing what it is beforehand
void example_2() {
    print("--- EXAMPLE 2 ---\n");
    using CrackUtils_t = CrackUtils<>;

    // setup
    auto crack = CrackUtils_t();

    // hashed string we want to crack
    uint64_t hashed = 0xC5BE054CB26B3829;

    // var to store cracked string if we find it
    string result;

    // max string length to try to crack
    constexpr int MAX_LEN = 10;

    // crack
    print("Trying to crack: {:#x}\n", hashed);

    // note the use of brute_n, this just runs try_crack_single with lengths [1, MAX_LEN]
    if (crack.brute_n(result, hashed, MAX_LEN)) {
        print("Found! {}\n", result);
    } else {
        print("Failed ):\n");
    }

    print("\n");
}

// cracking hash that's truncated to 63 bits which uses a different offset basis and prime
void example_3() {
    print("--- EXAMPLE 3 ---\n");
    constexpr uint64_t OFFSET_BASIS = 0xE4A68FF7D4912FD2;
    constexpr uint64_t PRIME = PRIME_233;
    constexpr uint32_t BIT_LEN = 63;

    using FNV_t = FNVUtilStatic<OFFSET_BASIS, PRIME, BIT_LEN>;
    using CrackUtils_t = CrackUtils<OFFSET_BASIS, PRIME, BIT_LEN>;

    // setup (everything below is copied from the first example)
    auto crack = CrackUtils_t();
    string to_hash = "known/crackmelol.exe";
    uint64_t hashed = FNV_t::hash(to_hash);

    // var to store cracked string if found
    string result;

    // max string length without needing to brute force is 8
    // since the string we want to crack is of length 10, we need 2 chars of brute force
    constexpr int BRUTE_CHARS = 2;

    // adding known prefix and suffix (useful if you're trying to crack a hashed filename)
    const string known_prefix = "known/";
    const string known_suffix = ".exe";
    
    // crack
    print("Trying to crack: {:#x}\n", hashed);
    if (crack.try_crack_single(result, hashed, to_hash.size(), BRUTE_CHARS, known_prefix, known_suffix)) {
        print("Found! {}\n", result);
    } else {
        print("Failed ):\n");
    }

    print("\n");
}

// cracking hash and using a character list to help avoid returning weird collisions
void example_4() {
    print("--- EXAMPLE 4 ---\n");

    using FNV_t = FNVUtilStatic<>;
    using CrackUtils_t = CrackUtils<>;

    // list of valid characters
    string charset = "0123456789abcdef";

    // you can also use some of the presets defined in crack.hpp
    /*
    namespace presets {
        static std::string valid = "0123456789abcdefghijklmnopqrstuvwxyz!\"#$%&'()*+,-./:;<=>?@[]^_`{|}~ ";
        static std::string valid_func = "0123456789abcdefghijklmnopqrstuvwxyz_";
        static std::string valid_file = "0123456789abcdefghijklmnopqrstuvwxyz_./";
        static std::string valid_gsc = "0123456789abcdefghijklmnopqrstuvwxyz_./:";
    }

    Ex: string charset = presets::valid_file;
    */

    // setup
    auto crack = CrackUtils_t(charset);

    // you can set the characters using in the brute force section too
    crack.set_bruting_charset(charset);

    string to_hash = "abc9784def";
    uint64_t hashed = FNV_t::hash(to_hash);

    // var to store cracked string if found
    string result;

    // max string length without needing to brute force is 8
    // since the string we want to crack is of length 10, we need 2 chars of brute force
    constexpr int BRUTE_CHARS = 2;
    
    // crack
    print("Trying to crack: {:#x}\n", hashed);
    if (crack.try_crack_single(result, hashed, to_hash.size(), BRUTE_CHARS)) {
        print("Found! {}\n", result);
    } else {
        print("Failed ):\n");
    }

    print("\n");
}

// example usages
int main() {
    example_1();
    example_2();
    example_3();
    example_4();

    return 0;
}